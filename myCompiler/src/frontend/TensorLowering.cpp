#include "TensorLowering.h"
#include "../common/Common.h"
#include <stdexcept>
#include <unordered_map>
#include <functional>
#include <sstream>

using namespace ast;

namespace
{
using Shape = std::vector<int>;

//获取常量int
int constInt(const Ptr<ExprNode> &e)
{
    if (auto il = castPtr<IntLiteralExprNode>(e))
        return il->value;
    if (auto un = castPtr<UnaryExprNode>(e))
    {
        if (un->op == UnaryOp::Minus)
            return -constInt(un->operand);
        if (un->op == UnaryOp::Plus)
            return constInt(un->operand);
    }
    throw std::runtime_error("Tensor dimension must be a constant,line: " +
                             std::to_string(e ? e->line : 0));
}
//保留维度信息
Shape dimsOf(const DataType &ty)
{
    Shape s;
    for (auto &e : ty.arraySizes())
    {
        int v = constInt(e);
        s.push_back(v > 0 ? v : -1);
    }
    return s;
}
//复制原来的数组type
DataType arrayType(PrimaryDataType base, const Shape &s)
{
    Vector<Ptr<ExprNode>> idxs;
    for (int d : s)
        idxs.emplace_back(makePtr<IntLiteralExprNode>(d < 0 ? -1 : d));
    return DataType(base, std::move(idxs));
}
//写回维度，之前没确定为-1
void fillShape(Shape &dst, const Shape &from)
{
    for (size_t i = 0; i < dst.size() && i < from.size(); ++i)
        if (dst[i] < 0 && from[i] > 0)
            dst[i] = from[i];
}
//判断是否为支持的tensor二元运算
bool isElemOp(BinaryOp op)
{
    return op == BinaryOp::Add || op == BinaryOp::Sub || op == BinaryOp::Mul ||
           op == BinaryOp::Div || op == BinaryOp::Mod;
}

struct FuncSig
{
    bool hasSret = false;
    //返回值形状
    Shape retShape;
    //返回值基础类型
    PrimaryDataType retBase = PrimaryDataType::INT;
    std::vector<char> omitFirst;
    std::vector<Shape> paramShapes;
};

struct Lowering
{
    unsigned tmp = 0;
    unsigned line = 0;
    //签名
    std::unordered_map<std::string, FuncSig> sigs;
    //全局数组维度
    std::unordered_map<std::string, Shape> gloabl_shapes;
    //std::unordered_map<std::string, PrimaryDataType> gloabl_bases;
    //当前数组维度信息
    std::unordered_map<std::string, Shape> shapes;
    //数组基类信息
    std::unordered_map<std::string, PrimaryDataType> bases;
    std::unordered_map<std::string, std::string> extOf;
    //当前函数签名
    FuncSig *cur = nullptr;
    
    std::string fresh(const char *p)
    {
        return std::string(p) + std::to_string(tmp++);
    }

    template <class T>
    Ptr<T> tagged(Ptr<T> n)
    {
        n->line = line;
        return n;
    }
    //生成一个字面量表达式
    Ptr<IntLiteralExprNode> lit(int v)
    {
        auto n = makePtr<IntLiteralExprNode>(v);
        n->line = line;
        return n;
    }
    Ptr<LValueExprNode> lval(const std::string &id, Vector<Ptr<ExprNode>> idx = {})
    {
        auto n = makePtr<LValueExprNode>(id, std::move(idx));
        n->line = line;
        return n;
    }
    Ptr<BinaryExprNode> bin(Ptr<ExprNode> a, Ptr<ExprNode> b, BinaryOp op)
    {
        auto n = makePtr<BinaryExprNode>(std::move(a), std::move(b), op);
        n->line = line;
        return n;
    }
    Ptr<DeclStmtNode> declScalar(const std::string &id, Ptr<ExprNode> init)
    {
        auto n = makePtr<DeclStmtNode>(DataType(PrimaryDataType::INT), id,
                                       makePtr<InitExprNode>(std::move(init)));
        n->line = line;
        return n;
    }
    Ptr<DeclStmtNode> declArray(const std::string &id, PrimaryDataType base, const Shape &s)
    {
        auto n = makePtr<DeclStmtNode>(arrayType(base, s), id);
        n->line = line;
        shapes[id] = s;
        bases[id] = base;
        return n;
    }
    Ptr<AssignStmtNode> assign(Ptr<LValueExprNode> lv, Ptr<ExprNode> rv)
    {
        auto n = makePtr<AssignStmtNode>(std::move(lv), std::move(rv));
        n->line = line;
        return n;
    }

    bool isArrayName(const std::string &id) const
    {
        auto it = shapes.find(id);
        return it != shapes.end() && !it->second.empty();
    }

    bool isTensorValued(const Ptr<ExprNode> &e)
    {
        if (auto lv = castPtr<LValueExprNode>(e))
            return lv->indices.empty() && isArrayName(lv->identifier);
        if (auto call = castPtr<CallExprNode>(e))
        {
            auto it = sigs.find(call->callee);
            return it != sigs.end() && it->second.hasSret;
        }
        if (auto un = castPtr<UnaryExprNode>(e))
            return isTensorValued(un->operand);
        if (auto b = castPtr<BinaryExprNode>(e))
        {
            if (b->op == BinaryOp::MatMul)
            {
                Shape ls = inferShape(b->left, {});
                Shape rs = inferShape(b->right, {});
                //1维乘以1维是标量
                return !(ls.size() == 1 && rs.size() == 1);
            }
            if (isElemOp(b->op))
                return isTensorValued(b->left) || isTensorValued(b->right);
        }
        return false;
    }
    //推断维度信息
    Shape inferShape(const Ptr<ExprNode> &e, const Shape &hint)
    {
        if (auto lv = castPtr<LValueExprNode>(e))
        {
            if (!lv->indices.empty())
                return {};
            Shape s = shapes.count(lv->identifier) ? shapes[lv->identifier] : Shape{};
            fillShape(s, hint);
            return s;
        }
        if (auto call = castPtr<CallExprNode>(e))
        {
            auto it = sigs.find(call->callee);
            if (it != sigs.end() && it->second.hasSret)
                return it->second.retShape;
            return {};
        }
        if (auto un = castPtr<UnaryExprNode>(e))
        {
            Shape s = inferShape(un->operand, hint);
            fillShape(s, hint);
            return s;
        }
        if (auto b = castPtr<BinaryExprNode>(e))
        {
            if (b->op == BinaryOp::MatMul)
            {
                Shape ld = inferShape(b->left, {});
                Shape rd = inferShape(b->right, {});
                if (ld.size() == 2 && rd.size() == 2)
                {
                    if (ld[1] < 0 && rd[0] > 0)
                        ld[1] = rd[0];
                    if (rd[0] < 0 && ld[1] > 0)
                        rd[0] = ld[1];
                    if (ld[0] < 0 && hint.size() >= 1)
                        ld[0] = hint[0];
                    if (rd[1] < 0 && hint.size() >= 2)
                        rd[1] = hint[1];
                    return {ld[0], rd[1]};
                }
                if (ld.size() == 2 && rd.size() == 1)
                {
                    if (rd[0] < 0 && ld[1] > 0)
                        rd[0] = ld[1];
                    if (ld[1] < 0 && rd[0] > 0)
                        ld[1] = rd[0];
                    if (ld[0] < 0 && !hint.empty())
                        ld[0] = hint[0];
                    return {ld[0]};
                }
                if (ld.size() == 1 && rd.size() == 2)
                {
                    if (ld[0] < 0 && rd[0] > 0)
                        ld[0] = rd[0];
                    if (rd[0] < 0 && ld[0] > 0)
                        rd[0] = ld[0];
                    if (rd[1] < 0 && !hint.empty())
                        rd[1] = hint[0];
                    return {rd[1]};
                }
                return {};
            }
            if (isElemOp(b->op))
            {
                Shape s = isTensorValued(b->left) ? inferShape(b->left, hint)
                                                  : inferShape(b->right, hint);
                if (isTensorValued(b->left) && isTensorValued(b->right))
                    fillShape(s, inferShape(b->right, hint));
                fillShape(s, hint);
                return s;
            }
        }
        return {};
    }

    Ptr<ExprNode> boundOf(const Shape &sh, size_t axis, const std::string &fallbackName)
    {
        if (axis < sh.size() && sh[axis] > 0)
            return lit(sh[axis]);
        if (axis == 0 && extOf.count(fallbackName))
            return lval(extOf[fallbackName]);
        throw std::runtime_error("Omitted tensor first dimension cannot be inferred,line: " +
                                 std::to_string(line));
    }
    ///生成循环
    void emitLoop(Ptr<ExprNode> limit, const std::function<void(const std::string &, Vector<Ptr<StmtNode>> &)> &body,
                  Vector<Ptr<StmtNode>> &out)
    {
        std::string iv = fresh("__i");
        out.push_back(declScalar(iv, lit(0)));
        Vector<Ptr<StmtNode>> inner;
        body(iv, inner);
        inner.push_back(assign(lval(iv), bin(lval(iv), lit(1), BinaryOp::Add)));
        auto wh = makePtr<WhileStmtNode>(bin(lval(iv), std::move(limit), BinaryOp::Lt),
                                         makePtr<BlockStmtNode>(std::move(inner)));
        //方便调试
        wh->line = 10000 + tmp;
        out.push_back(wh);
    }

    void emitNest(const Shape &sh, const std::string &boundName,
                  const std::function<void(const Vector<Ptr<ExprNode>> &, Vector<Ptr<StmtNode>> &)> &body,
                  Vector<Ptr<StmtNode>> &out)
    {
        std::function<void(size_t, Vector<Ptr<ExprNode>> &, Vector<Ptr<StmtNode>> &)> rec =
            [&](size_t dim, Vector<Ptr<ExprNode>> &idx, Vector<Ptr<StmtNode>> &dst)
        {
            if (dim == sh.size())
            {
                body(idx, dst);
                return;
            }
            emitLoop(boundOf(sh, dim, boundName), [&](const std::string &iv, Vector<Ptr<StmtNode>> &inner)
                     {
                         idx.push_back(lval(iv));
                         rec(dim + 1, idx, inner);
                         idx.pop_back();
                     },
                     dst);
        };
        Vector<Ptr<ExprNode>> idx;
        rec(0, idx, out);
    }
    //[name,shape]
    std::pair<std::string, Shape> materialize(const Ptr<ExprNode> &e, const Shape &hint,
                                              Vector<Ptr<StmtNode>> &out)
    {
        if (auto lv = castPtr<LValueExprNode>(e))
        {
            if (lv->indices.empty() && isArrayName(lv->identifier))
            {
                Shape s = shapes[lv->identifier];
                fillShape(s, hint);
                return {lv->identifier, s};
            }
        }
        Shape s = inferShape(e, hint);
        fillShape(s, hint);
        if (s.empty())
            throw std::runtime_error("Cannot infer tensor shape,line: " + std::to_string(line));
        for (int d : s)
            if (d <= 0)
                throw std::runtime_error("Omitted tensor first dimension cannot be inferred,line: " +
                                         std::to_string(line));
        std::string name = fresh("__t");
        out.push_back(declArray(name, PrimaryDataType::INT, s));
        lowerInto(e, name, s, out);
        return {name, s};
    }

    Ptr<ExprNode> indexed(const std::string &name, const Vector<Ptr<ExprNode>> &idx)
    {
        Vector<Ptr<ExprNode>> copy;
        for (auto &i : idx)
            copy.push_back(i);
        return lval(name, std::move(copy));
    }

    void lowerInto(const Ptr<ExprNode> &e, const std::string &dest, const Shape &destSh,
                   Vector<Ptr<StmtNode>> &out)
    {
        if (auto call = castPtr<CallExprNode>(e))
        {
            auto it = sigs.find(call->callee);
            if (it == sigs.end() || !it->second.hasSret)
                throw std::runtime_error("Invalid tensor call,line: " + std::to_string(line));
            Vector<Ptr<ExprNode>> args;
            args.push_back(lval(dest));
            for (size_t i = 0; i < call->args.size(); ++i)
            {
                if (isTensorValued(call->args[i]))
                {
                    auto [nm, sh] = materialize(call->args[i], {}, out);
                    args.push_back(lval(nm));
                    if (i < it->second.omitFirst.size() && it->second.omitFirst[i])
                    {
                        if (!sh.empty() && sh[0] > 0)
                            args.push_back(lit(sh[0]));
                        else if (extOf.count(nm))
                            args.push_back(lval(extOf[nm]));
                        else
                            throw std::runtime_error("Cannot pass tensor with unknown first dimension,line: " +
                                                     std::to_string(line));
                    }
                }
                else
                {
                    args.push_back(call->args[i]);
                    if (i < it->second.omitFirst.size() && it->second.omitFirst[i])
                        throw std::runtime_error("Tensor parameter expects a tensor,line: " +
                                                 std::to_string(line));
                }
            }
            auto c = makePtr<CallExprNode>(call->callee, std::move(args));
            c->line = line;
            auto es = makePtr<ExprStmtNode>(c);
            es->line = line;
            out.push_back(es);
            return;
        }
        if (auto lv = castPtr<LValueExprNode>(e))
        {
            if (lv->indices.empty() && isArrayName(lv->identifier))
            {
                if (lv->identifier == dest)
                    return;
                emitNest(destSh, dest, [&](const Vector<Ptr<ExprNode>> &idx, Vector<Ptr<StmtNode>> &b)
                         { b.push_back(assign(lval(dest, [&]
                                                   {
                                                       Vector<Ptr<ExprNode>> c;
                                                       for (auto &x : idx)
                                                           c.push_back(x);
                                                       return c;
                                                   }()),
                                              indexed(lv->identifier, idx))); },
                         out);
                return;
            }
        }
        if (auto un = castPtr<UnaryExprNode>(e))
        {
            if (un->op == UnaryOp::Plus)
            {
                lowerInto(un->operand, dest, destSh, out);
                return;
            }
            if (un->op == UnaryOp::Minus)
            {
                auto [nm, sh] = materialize(un->operand, destSh, out);
                emitNest(destSh, dest, [&](const Vector<Ptr<ExprNode>> &idx, Vector<Ptr<StmtNode>> &b)
                         {
                             b.push_back(assign(lval(dest, [&]
                                                     {
                                                         Vector<Ptr<ExprNode>> c;
                                                         for (auto &x : idx)
                                                             c.push_back(x);
                                                         return c;
                                                     }()),
                                                [&]() {
                                                    auto u = makePtr<UnaryExprNode>(indexed(nm, idx), UnaryOp::Minus);
                                                    u->line = line;
                                                    return u;
                                                }()));
                         },
                         out);
                return;
            }
        }
        if (auto bop = castPtr<BinaryExprNode>(e))
        {
            if (bop->op == BinaryOp::MatMul)
            {
                emitMatMul(bop, dest, destSh, out);
                return;
            }
            if (isElemOp(bop->op))
            {
                bool lT = isTensorValued(bop->left);
                bool rT = isTensorValued(bop->right);
                std::string ln, rn;
                if (lT)
                    ln = materialize(bop->left, destSh, out).first;
                if (rT)
                    rn = materialize(bop->right, destSh, out).first;
                emitNest(destSh, dest, [&](const Vector<Ptr<ExprNode>> &idx, Vector<Ptr<StmtNode>> &b)
                         {
                             Ptr<ExprNode> a = lT ? indexed(ln, idx) : bop->left;
                             Ptr<ExprNode> c = rT ? indexed(rn, idx) : bop->right;
                             b.push_back(assign(lval(dest, [&]
                                                     {
                                                         Vector<Ptr<ExprNode>> cp;
                                                         for (auto &x : idx)
                                                             cp.push_back(x);
                                                         return cp;
                                                     }()),
                                                bin(std::move(a), std::move(c), bop->op)));
                         },
                         out);
                return;
            }
        }
        throw std::runtime_error("Unsupported tensor expression,line: " + std::to_string(line));
    }

    void emitMatMul(const Ptr<BinaryExprNode> &bop, const std::string &dest, const Shape &destSh,
                    Vector<Ptr<StmtNode>> &out)
    {
        auto [ln, ld] = materialize(bop->left, {}, out);
        auto [rn, rd] = materialize(bop->right, {}, out);
        if (ld.size() == 2 && rd.size() == 2)
        {
            if (ld[1] < 0 && rd[0] > 0)
                ld[1] = rd[0];
            if (rd[0] < 0 && ld[1] > 0)
                rd[0] = ld[1];
            int M = destSh.size() >= 1 && destSh[0] > 0 ? destSh[0] : ld[0];
            int N = destSh.size() >= 2 && destSh[1] > 0 ? destSh[1] : rd[1];
            int K = ld[1];
            if (M <= 0 || N <= 0 || K <= 0)
                throw std::runtime_error("Matrix multiply shape unknown,line: " + std::to_string(line));
            emitLoop(lit(M), [&](const std::string &i, Vector<Ptr<StmtNode>> &bi)
                     { emitLoop(lit(N), [&](const std::string &j, Vector<Ptr<StmtNode>> &bj)
                                {
                                    //累加器
                                    std::string acc = fresh("__acc");
                                    bj.push_back(declScalar(acc, lit(0)));
                                    //内层k
                                    emitLoop(lit(K), [&](const std::string &k, Vector<Ptr<StmtNode>> &bk)
                                             {
                                                 auto mul = bin(indexed(ln, {lval(i), lval(k)}),
                                                                indexed(rn, {lval(k), lval(j)}),
                                                                BinaryOp::Mul);
                                                 bk.push_back(assign(lval(acc), bin(lval(acc), std::move(mul), BinaryOp::Add)));
                                             },
                                             bj);
                                    bj.push_back(assign(lval(dest, {lval(i), lval(j)}), lval(acc)));
                                },
                                bi); },
                     out);
            return;
        }
        if (ld.size() == 2 && rd.size() == 1)
        {
            if (rd[0] < 0 && ld[1] > 0)
                rd[0] = ld[1];
            int M = destSh.size() >= 1 && destSh[0] > 0 ? destSh[0] : ld[0];
            int K = ld[1] > 0 ? ld[1] : rd[0];
            if (M <= 0 || K <= 0)
                throw std::runtime_error("Matrix-vector shape unknown,line: " + std::to_string(line));
            emitLoop(lit(M), [&](const std::string &i, Vector<Ptr<StmtNode>> &bi)
                     {
                         std::string acc = fresh("__acc");
                         bi.push_back(declScalar(acc, lit(0)));
                         emitLoop(lit(K), [&](const std::string &k, Vector<Ptr<StmtNode>> &bk)
                                  {
                                      auto mul = bin(indexed(ln, {lval(i), lval(k)}), indexed(rn, {lval(k)}), BinaryOp::Mul);
                                      bk.push_back(assign(lval(acc), bin(lval(acc), std::move(mul), BinaryOp::Add)));
                                  },
                                  bi);
                         bi.push_back(assign(lval(dest, {lval(i)}), lval(acc)));
                     },
                     out);
            return;
        }
        if (ld.size() == 1 && rd.size() == 2)
        {
            if (ld[0] < 0 && rd[0] > 0)
                ld[0] = rd[0];
            int K = ld[0] > 0 ? ld[0] : rd[0];
            int N = destSh.size() >= 1 && destSh[0] > 0 ? destSh[0] : rd[1];
            if (K <= 0 || N <= 0)
                throw std::runtime_error("Vector-matrix shape unknown,line: " + std::to_string(line));
            emitLoop(lit(N), [&](const std::string &j, Vector<Ptr<StmtNode>> &bj)
                     {
                         std::string acc = fresh("__acc");
                         bj.push_back(declScalar(acc, lit(0)));
                         emitLoop(lit(K), [&](const std::string &k, Vector<Ptr<StmtNode>> &bk)
                                  {
                                      auto mul = bin(indexed(ln, {lval(k)}), indexed(rn, {lval(k), lval(j)}), BinaryOp::Mul);
                                      bk.push_back(assign(lval(acc), bin(lval(acc), std::move(mul), BinaryOp::Add)));
                                  },
                                  bj);
                         bj.push_back(assign(lval(dest, {lval(j)}), lval(acc)));
                     },
                     out);
            return;
        }
        throw std::runtime_error("Unsupported @ ranks,line: " + std::to_string(line));
    }

    Ptr<ExprNode> lowerScalar(const Ptr<ExprNode> &e, Vector<Ptr<StmtNode>> &out)
    {
        if (!e)
            return e;
        if (auto bop = castPtr<BinaryExprNode>(e))
        {
            //点乘为3层循环单独处理一个
            if (bop->op == BinaryOp::MatMul)
            {
                Shape ld = inferShape(bop->left, {});
                Shape rd = inferShape(bop->right, {});
                //a[][k]@b[k][]
                if (ld.size() == 1 && rd.size() == 1)
                {
                    auto [ln, ls] = materialize(bop->left, {}, out);
                    auto [rn, rs] = materialize(bop->right, {}, out);
                    int K = ls[0] > 0 ? ls[0] : rs[0];
                    if (K <= 0)
                        throw std::runtime_error("Inner product length unknown,line: " + std::to_string(line));
                    std::string acc = fresh("__acc");
                    out.push_back(declScalar(acc, lit(0)));
                    //生成一个内层循环
                    emitLoop(lit(K), [&](const std::string &k, Vector<Ptr<StmtNode>> &bk)
                             {
                                 auto mul = bin(indexed(ln, {lval(k)}), indexed(rn, {lval(k)}), BinaryOp::Mul);
                                 bk.push_back(assign(lval(acc), bin(lval(acc), std::move(mul), BinaryOp::Add)));
                             },
                             out);
                    return lval(acc);
                }
            }
            if (isTensorValued(e))
                throw std::runtime_error("Tensor value used in scalar context,line: " + std::to_string(line));
            bop->left = lowerScalar(bop->left, out);
            bop->right = lowerScalar(bop->right, out);
            return bop;
        }
        if (auto un = castPtr<UnaryExprNode>(e))
        {
            un->operand = lowerScalar(un->operand, out);
            return un;
        }
        if (auto call = castPtr<CallExprNode>(e))
        {
            auto it = sigs.find(call->callee);
            if (it != sigs.end() && it->second.hasSret)
                throw std::runtime_error("Tensor-returning call must be assigned,line: " + std::to_string(line));
            if (it != sigs.end())
            {
                Vector<Ptr<ExprNode>> args;
                for (size_t i = 0; i < call->args.size(); ++i)
                {
                    if (isTensorValued(call->args[i]))
                    {
                        auto [nm, sh] = materialize(call->args[i], {}, out);
                        args.push_back(lval(nm));
                        if (i < it->second.omitFirst.size() && it->second.omitFirst[i])
                        {
                            if (!sh.empty() && sh[0] > 0)
                                args.push_back(lit(sh[0]));
                            else if (extOf.count(nm))
                                args.push_back(lval(extOf[nm]));
                            else
                                throw std::runtime_error("Cannot pass tensor with unknown first dimension,line: " +
                                                         std::to_string(line));
                        }
                    }
                    else
                    {
                        args.push_back(lowerScalar(call->args[i], out));
                        if (i < it->second.omitFirst.size() && it->second.omitFirst[i])
                            throw std::runtime_error("Tensor parameter expects a tensor,line: " +
                                                     std::to_string(line));
                    }
                }
                call->args = std::move(args);
            }
            return call;
        }
        return e;
    }

    Vector<Ptr<StmtNode>> lowerStmtList(const Vector<Ptr<StmtNode>> &stmts)
    {
        Vector<Ptr<StmtNode>> out;
        for (auto &s : stmts)
            emitStmt(s, out);
        return out;
    }

    Ptr<StmtNode> asStmt(Vector<Ptr<StmtNode>> &&ss)
    {
        if (ss.size() == 1)
            return ss[0];
        auto b = makePtr<BlockStmtNode>(std::move(ss));
        b->line = line;
        return b;
    }
    //改写原来的语句
    void emitStmt(const Ptr<StmtNode> &s, Vector<Ptr<StmtNode>> &out)
    {
        if (!s)
            return;
        line = s->line;
        if (auto blk = castPtr<BlockStmtNode>(s))
        {
            blk->stmts = lowerStmtList(blk->stmts);
            out.push_back(blk);
            return;
        }
        if (auto decl = castPtr<DeclStmtNode>(s))
        {
            if (decl->type.isArray())
            {
                shapes[decl->identifier] = dimsOf(decl->type);
                bases[decl->identifier] = decl->type.baseType;
            }
            //这里目前有bug，tensor初始化列表赋值有问题
            else if (decl->initializer && decl->initializer->singleInitVal)
            {
                decl->initializer->singleInitVal = lowerScalar(decl->initializer->singleInitVal, out);
            }
            out.push_back(decl);
            return;
        }
        if (auto asg = castPtr<AssignStmtNode>(s))
        {
            //tensor赋值
            if (asg->lvalue->indices.empty() && isArrayName(asg->lvalue->identifier))
            {
                Shape dsh = shapes[asg->lvalue->identifier];
                lowerInto(asg->rvalue, asg->lvalue->identifier, dsh, out);
                return;
            }
            //表达式降为标量
            asg->rvalue = lowerScalar(asg->rvalue, out);
            out.push_back(asg);
            return;
        }
        if (auto ret = castPtr<ReturnStmtNode>(s))
        {
            if (cur && cur->hasSret && ret->ret_expr)
            {
                lowerInto(ret->ret_expr, "__sret", cur->retShape, out);
                auto r = makePtr<ReturnStmtNode>(nullptr);
                r->line = line;
                out.push_back(r);
                return;
            }
            if (ret->ret_expr)
                ret->ret_expr = lowerScalar(ret->ret_expr, out);
            out.push_back(ret);
            return;
        }
        if (auto es = castPtr<ExprStmtNode>(s))
        {
            if (es->expr)
                es->expr = lowerScalar(es->expr, out);
            out.push_back(es);
            return;
        }
        if (auto iff = castPtr<IfElseStmtNode>(s))
        {
            iff->condition = lowerScalar(iff->condition, out);
            Vector<Ptr<StmtNode>> t, el;
            emitStmt(iff->then_body, t);
            iff->then_body = asStmt(std::move(t));
            if (iff->else_body)
            {
                emitStmt(iff->else_body, el);
                iff->else_body = asStmt(std::move(el));
            }
            out.push_back(iff);
            return;
        }
        if (auto wh = castPtr<WhileStmtNode>(s))
        {
            Vector<Ptr<StmtNode>> pre;
            wh->condition = lowerScalar(wh->condition, pre);
            out.insert(out.end(), pre.begin(), pre.end());
            Vector<Ptr<StmtNode>> body;
            emitStmt(wh->body, body);
            wh->body = asStmt(std::move(body));
            out.push_back(wh);
            return;
        }
        out.push_back(s);
    }

    void walkStmt(const Ptr<StmtNode> &s, const std::function<void(const Ptr<StmtNode> &)> &fn)
    {
        if (!s)
            return;
        fn(s);
        if (auto b = castPtr<BlockStmtNode>(s))
        {
            for (auto &x : b->stmts)
                walkStmt(x, fn);
        }
        else if (auto iff = castPtr<IfElseStmtNode>(s))
        {
            walkStmt(iff->then_body, fn);
            walkStmt(iff->else_body, fn);
        }
        else if (auto wh = castPtr<WhileStmtNode>(s))
            walkStmt(wh->body, fn);
    }

    Shape inferTensorReturnShape(const Ptr<FuncNode> &f)
    {
        auto savedShapes = shapes;
        auto savedBases = bases;
        shapes.clear();
        bases.clear();
        for (auto &p : f->params)
        {
            if (p->type.isArray())
            {
                shapes[p->identifier] = dimsOf(p->type);
                bases[p->identifier] = p->type.baseType;
            }
        }
        if (f->body)
        {
            walkStmt(f->body, [&](const Ptr<StmtNode> &n)
                     {
                         if (auto d = castPtr<DeclStmtNode>(n))
                         {
                             if (d->type.isArray())
                             {
                                 shapes[d->identifier] = dimsOf(d->type);
                                 bases[d->identifier] = d->type.baseType;
                             }
                         }
                     });
        }
        Shape found;
        if (f->body)
        {
            walkStmt(f->body, [&](const Ptr<StmtNode> &n)
                     {
                         if (auto r = castPtr<ReturnStmtNode>(n))
                         {
                             if (r->ret_expr)
                             {
                                 Shape sh = inferShape(r->ret_expr, {});
                                 if (!sh.empty())
                                     found = sh;
                             }
                         }
                     });
        }
        shapes = std::move(savedShapes);
        bases = std::move(savedBases);
        return found;
    }

    void rewriteSignature(Ptr<FuncNode> f)
    {
        FuncSig sig;
        auto userParams = f->params;
        if (f->returnType.isTensor)
        {
            //tensor类型返回值要按照规则改写函数签名，这里改写返回类型
            sig.hasSret = true;
            sig.retShape = inferTensorReturnShape(f);
            sig.retBase = f->returnType.baseType;
            if (sig.retShape.empty())
                throw std::runtime_error("Cannot infer tensor return shape of '" + f->identifier + "'");
            Shape sretSh = sig.retShape;
            if (!sretSh.empty())
                sretSh[0] = -1;
            auto sret = makePtr<DeclStmtNode>(arrayType(sig.retBase, sretSh), "__sret", nullptr, true);
            sret->line = f->body ? f->body->line : 0;
            f->params.clear();
            f->params.push_back(sret);
            f->returnType = DataType(PrimaryDataType::VOID);
        }
        else
            f->params.clear();
        //重构函数参数类型
        for (auto &p : userParams)
        {
            f->params.push_back(p);
            Shape sh = p->type.isArray() ? dimsOf(p->type) : Shape{};
            sig.paramShapes.push_back(sh);
            //是否为数组参数，省略维度这个时候indice传入是-1
            char omit = (!sh.empty() && sh[0] < 0) ? 1 : 0;
            sig.omitFirst.push_back(omit);
            //数组参数最外层维度，本来ir不需要，这里要添加用于推导函数内tensor类型
            if (omit)
            {
                auto ext = makePtr<DeclStmtNode>(DataType(PrimaryDataType::INT), p->identifier + "__ext",
                                                 nullptr, true);
                ext->line = p->line;
                f->params.push_back(ext);
            }
        }
        sigs[f->identifier] = std::move(sig);
    }

    void lowerFunc(Ptr<FuncNode> f)
    {
        //进入新函数只保留全局的数据
        shapes.clear();
        shapes=gloabl_shapes;
        bases.clear();
        extOf.clear();
        //函数签名
        cur = &sigs[f->identifier];
        tmp = 0;
        if (cur->hasSret)
        {
            shapes["__sret"] = cur->retShape;
            bases["__sret"] = cur->retBase;
        }
        //如果有返回值则要插入一个ret数组把函数模板变为void
        size_t pi = cur->hasSret ? 1 : 0;
        for (size_t i = 0; i < cur->paramShapes.size(); ++i)
        {
            auto &p = f->params[pi++];
            //记录函数形参形状
            if (!cur->paramShapes[i].empty())
            {
                shapes[p->identifier] = cur->paramShapes[i];
                bases[p->identifier] = p->type.baseType;
            }
            //如果是数组则增加参数记录外层维度
            if (cur->omitFirst[i])
            {
                auto &ext = f->params[pi++];
                extOf[p->identifier] = ext->identifier;
            }
        }
        if (f->body)
            f->body->stmts = lowerStmtList(f->body->stmts);
        cur = nullptr;
    }
};
} // namespace

//前端处理tensor的入口
void ast::lowerTensors(std::shared_ptr<CompUnitNode> compUnit)
{
    //不含tensor不处理，不用lowering
    if (!compUnit||!CompilerConfig::isTensorProgram)
        return;
    Lowering L;
    for (auto &ch : compUnit->children)
    {
        //全局声明
        if (auto d = castPtr<DeclStmtNode>(ch))
        {
            if (d->type.isArray())
                L.gloabl_shapes[d->identifier] = dimsOf(d->type);
        }
        //如果是函数节点则重写函数签名
        else if (auto f = castPtr<FuncNode>(ch))
            L.rewriteSignature(f);
    }
    //再遍历一遍，因为调用者要知道被调用者的返回类型才能推导本身的返回值类型
    for (auto &ch : compUnit->children)
    {
        if (auto f = castPtr<FuncNode>(ch))
            L.lowerFunc(f);
    }
}
