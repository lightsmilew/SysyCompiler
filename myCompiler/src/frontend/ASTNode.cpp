#include "ASTNode.h"

// 测试
namespace ast
{
    // 非成员函数：将 BinaryOp 转为字符串
    std::string toString(BinaryOp op)
    {
        switch (op)
        {
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Sub:
            return "-";
        case BinaryOp::Mul:
            return "*";
        case BinaryOp::Div:
            return "/";
        case BinaryOp::Mod:
            return "%";
        case BinaryOp::Lt:
            return "<";
        case BinaryOp::Gt:
            return ">";
        case BinaryOp::Le:
            return "<=";
        case BinaryOp::Ge:
            return ">=";
        case BinaryOp::Eq:
            return "==";
        case BinaryOp::Ne:
            return "!=";
        case BinaryOp::And:
            return "&&";
        case BinaryOp::Or:
            return "||";
        default:
            return "unknown";
        }
    }
    // Helper function to print indent
    void print_indent(std::ostream &out, unsigned indent)
    {
        out << std::string(indent, ' ');
    }

    // CompUnit
    string CompUnitNode::toString() const
    {
        return "CompUnitNode";
    }

    void CompUnitNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        for (const auto &child : children)
        {
            child->print(out, indent + 2);
        }
    }

    // BlockStmt
    string BlockStmtNode::toString() const
    {
        return "BlockStmtNode";
    }

    void BlockStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        for (const auto &stmt : stmts)
        {
            stmt->print(out, indent + 2);
        }
    }

    // ExprStmt
    string ExprStmtNode::toString() const
    {
        return "ExprStmtNode";
    }

    void ExprStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (expr)
        {
            expr->print(out, indent + 2);
        }
    }

    // DeclStmt
    string DeclStmtNode::toString() const
    {
        string constStr = type.isConst() ? "const " : "";
        return "DeclStmtNode: " + constStr + identifier;
    }

    void DeclStmtNode::print(ostream &out, unsigned indent) const
    {
        out << std::string(indent, ' ') << "DeclStmtNode: " << (type.isConst() ? "const " : "") << identifier << " : ";
        switch (type.baseType)
        {
        case PrimaryDataType::VOID:
            out << "void";
            break;
        case PrimaryDataType::INT:
            out << "int";
            break;
        case PrimaryDataType::FLOAT:
            out << "float";
            break;
        default:
            out << "unknown";
            break;
        }
        for (int i = 0; i < type.arrayDimensionCount(); ++i)
        {
            out << "[" << type.arraySizes()[i]->toString() << "]";
        }

        if (initializer)
        {
            out << " = ";
            initializer->print(out, 0);
        }

        out << std::endl;
    }

    // AssignStmt
    string AssignStmtNode::toString() const
    {
        return "AssignStmtNode";
    }

    void AssignStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (lvalue)
        {
            lvalue->print(out, indent + 2);
        }
        if (rvalue)
        {
            rvalue->print(out, indent + 2);
        }
    }

    // IfElseStmt
    string IfElseStmtNode::toString() const
    {
        return "IfElseStmtNode";
    }

    void IfElseStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (condition)
        {
            condition->print(out, indent + 2);
        }
        if (then_body)
        {
            then_body->print(out, indent + 2);
        }
        if (else_body)
        {
            else_body->print(out, indent + 2);
        }
    }

    // WhileStmt
    string WhileStmtNode::toString() const
    {
        return "WhileStmtNode";
    }

    void WhileStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (condition)
        {
            condition->print(out, indent + 2);
        }
        if (body)
        {
            body->print(out, indent + 2);
        }
    }

    // BreakStmt
    string BreakStmtNode::toString() const
    {
        return "BreakStmt";
    }

    // ContinueStmt
    string ContinueStmtNode::toString() const
    {
        return "ContinueStmt";
    }

    // ReturnStmt
    string ReturnStmtNode::toString() const
    {
        return "ReturnStmtNode";
    }

    void ReturnStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (ret_expr)
        {
            ret_expr->print(out, indent + 2);
        }
    } // InitExpr
    string InitExprNode::toString() const
    {
        if (singleInitVal)
        {
            return "InitExprNode: " + singleInitVal->toString();
        }
        else
        {
            string result = "InitExprNode: {";
            for (size_t i = 0; i < multiInitVal.size(); ++i)
            {
                result += multiInitVal[i]->toString();
                if (i < multiInitVal.size() - 1)
                {
                    result += ", ";
                }
            }
            result += "}";
            return result;
        }
    }

    // LValueExpr
    string LValueExprNode::toString() const
    {
        return "LValueExprNode: " + identifier;
    }

    void LValueExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        for (const auto &index : indices)
        {
            index->print(out, indent + 2);
        }
    }

    // BinaryExpr
    string BinaryExprNode::toString() const
    {
        if (op == BinaryOp::Add)
        {
            return "BinaryExprNode: " + left->toString() + " + " + right->toString();
        }
        else if (op == BinaryOp::Sub)
        {
            return "BinaryExprNode: " + left->toString() + " - " + right->toString();
        }
        else if (op == BinaryOp::Mul)
        {
            return "BinaryExprNode: " + left->toString() + " * " + right->toString();
        }
        else if (op == BinaryOp::Div)
        {
            return "BinaryExprNode: " + left->toString() + " / " + right->toString();
        }
        else if (op == BinaryOp::Mod)
        {
            return "BinaryExprNode: " + left->toString() + " % " + right->toString();
        }
        else if (op == BinaryOp::Lt)
        {
            return "BinaryExprNode: " + left->toString() + " < " + right->toString();
        }
        else if (op == BinaryOp::Gt)
        {
            return "BinaryExprNode: " + left->toString() + " > " + right->toString();
        }
        else if (op == BinaryOp::Le)
        {
            return "BinaryExprNode: " + left->toString() + " <= " + right->toString();
        }
        else if (op == BinaryOp::Ge)
        {
            return "BinaryExprNode: " + left->toString() + " >= " + right->toString();
        }
        else if (op == BinaryOp::Eq)
        {
            return "BinaryExprNode: " + left->toString() + " == " + right->toString();
        }
        else if (op == BinaryOp::Ne)
        {
            return "BinaryExprNode: " + left->toString() + " != " + right->toString();
        }
        else if (op == BinaryOp::And)
        {
            return "BinaryExprNode: " + left->toString() + " && " + right->toString();
        }
        else if (op == BinaryOp::Or)
        {
            return "BinaryExprNode: " + left->toString() + " || " + right->toString();
        }
        else
        {
            return "Unknown BinaryExprNode";
        }
    }

    void BinaryExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (left)
        {
            left->print(out, indent + 2);
        }
        if (right)
        {
            right->print(out, indent + 2);
        }
    }

    // UnaryExpr
    string UnaryExprNode::toString() const
    {
        if (op == UnaryOp::Plus)
        {
            return "UnaryExprNode: +" + operand->toString();
        }
        else if (op == UnaryOp::Minus)
        {
            return "UnaryExprNode: -" + operand->toString();
        }
        else if (op == UnaryOp::Not)
        {
            return "UnaryExprNode: !" + operand->toString();
        }
        else
        {
            return "Unknown UnaryExprNode";
        }
    }

    void UnaryExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        if (operand)
        {
            operand->print(out, indent + 2);
        }
    }

    // IntegerLiteralExpr
    string IntLiteralExprNode::toString() const
    {
        return "IntegerLiteralExprNode: " + std::to_string(value);
    }

    void IntLiteralExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
    }

    // FloatLiteralExpr
    string FloatLiteralExprNode::toString() const
    {
        return "FloatLiteralExprNode: " + std::to_string(value);
    }

    void FloatLiteralExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
    } // StringLiteralExpr
    string StringLiteralExprNode::toString() const
    {
        return "StringLiteralExprNode: \"" + value + "\"";
    }

    void StringLiteralExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
    }

    // CallExpr
    string CallExprNode::toString() const
    {
        string args_str;
        for (const auto &arg : args)
        {
            if (!args_str.empty())
                args_str += ", ";
            args_str += arg->toString();
        }
        return "CallExprNode: " + callee + "(" + args_str + ")";
    }
    void CallExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
        for (const auto &arg : args)
        {
            arg->print(out, indent + 2);
        }
    }

    // Function
    string FuncNode::toString() const
    {
        return "Function";
    }

    void FuncNode::print(std::ostream &out, unsigned indent) const
    {
        out << std::string(indent, ' ') << "Function" << std::endl;

        // 打印返回类型和函数名
        out << std::string(indent + 2, ' ');
        switch (returnType.baseType)
        {
        case PrimaryDataType::VOID:
            out << "void";
            break;
        case PrimaryDataType::INT:
            out << "int";
            break;
        case PrimaryDataType::FLOAT:
            out << "float";
            break;
        default:
            out << "unknown";
            break;
        }

        for (int i = 0; i < returnType.arrayDimensionCount(); ++i)
        {
            out << "[" << returnType.arraySizes()[i] << "]";
        }

        out << " " << identifier << "(" << std::endl;

        // 打印参数
        out << std::string(indent + 4, ' ') << "Parameters:" << std::endl;
        for (const auto &param : params)
        {
            param->print(out, indent + 6);
        }

        out << std::string(indent + 2, ' ') << ")" << std::endl;

        // 打印函数体
        if (body)
        {
            body->print(out, indent + 2);
        }
    }

}
