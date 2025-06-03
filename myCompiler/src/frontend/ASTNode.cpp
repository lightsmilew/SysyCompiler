#include "ASTNode.h"

namespace ast
{

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
        return "DeclStmtNode: " + identifier;
    }

    void DeclStmtNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
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
            out << "[" << type.arraySizes()[i] << "]";
        }

        if (initializer)
        {
            out << " = ";
            initializer->print(out, 0);
        }

        out << "\n";
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
        if (expr)
        {
            expr->print(out, indent + 2);
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
        return "BinaryExprNode: " + left->toString() + " " + op + " " + right->toString();
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
        return "UnaryExprNode: " + op + " " + operand->toString();
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
    string IntegerLiteralExprNode::toString() const
    {
        return "IntegerLiteralExprNode: " + to_string(value);
    }

    void IntegerLiteralExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
    }

    // FloatLiteralExpr
    string FloatLiteralExprNode::toString() const
    {
        return "FloatLiteralExprNode: " + value;
    }

    void FloatLiteralExprNode::print(ostream &out, unsigned indent) const
    {
        ASTNode::print(out, indent);
    }

    // StringLiteralExpr
    string StringLiteralExprNode::toString() const
    {
        return "StringLiteralExprNode: " + value;
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
        out << "Callee: " << callee << "\n";
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
        ASTNode::print(out, indent);
        // returnType 打印逻辑
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

        out << " " << identifier << "(";

        print_indent(out, indent + 2);
        out << "Parameters:\n";
        for (const auto &param : params)
        {
            param->print(out, indent + 4);
        }
        out << ")";

        if (body)
        {
            out << " ";
            body->print(out, indent);
        }
        else
        {
            out << ";";
        }

        out << "\n";
    }
}
