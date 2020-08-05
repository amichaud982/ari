#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expr.h"
#include "memory.h"
#include "parser.h"
#include "stmt.h"
#include "token.h"
#include "tokenizer.h"

static stmt *statement(parser *analyzer);
static expr *expression(parser *analyzer);
static token *advance(parser *analyzer);

static char *take_string(token *tok)
{
    int length = tok->length;
    char *buffer = ALLOCATE(char, length + 1);
    buffer = strncpy(buffer, tok->start, tok->length);
    buffer[tok->length] = '\0';
    return buffer;
}

static expr *init_expr(void)
{
    expr *new_expr = ALLOCATE(expr, 1);
    new_expr->type = 0;
    new_expr->name = NULL;
    new_expr->operator = NULL;
    new_expr->literal = NULL;
    new_expr->expression= NULL;
    new_expr->value = NULL;
    new_expr->left = NULL;
    new_expr->right = NULL;
    return new_expr;
}

static stmt *init_stmt(void)
{
    stmt *new_stmt = ALLOCATE(stmt, 1);
    new_stmt->type = 0;
    new_stmt->name = NULL;
    new_stmt->initializer = NULL;
    new_stmt->expression = NULL;
    new_stmt->condition = NULL;
    new_stmt->value = NULL;
    new_stmt->loopbody = NULL;
    new_stmt->thenbranch = NULL;
    new_stmt->elsebranch = NULL;
    new_stmt->count = 0;
    new_stmt->capacity = 0;
    new_stmt->stmts = NULL;
    return new_stmt;
}

static void delete_expression(expr *pexpr)
{
    if (pexpr->expression)
        delete_expression(pexpr->expression);
    if (pexpr->value)
        delete_expression(pexpr->value);
    if (pexpr->left)
        delete_expression(pexpr->left);
    if (pexpr->right)
        delete_expression(pexpr->right);
    free(pexpr->literal);
    free(pexpr);
}

static void delete_statements(stmt *pstmt)
{
    switch (pstmt->type) {
        case STMT_BLOCK:
            for (int i = 0; i < pstmt->count; i++)
                delete_statements(pstmt->stmts[i]);
            break;
        case STMT_EXPR:
            break;
        case STMT_IF:
            delete_statements(pstmt->thenbranch);
            if (pstmt->elsebranch)
                delete_statements(pstmt->elsebranch);
            break;
        case STMT_WHILE:
            delete_statements(pstmt->loopbody);
            break;
        case STMT_FOR:
            for (int i = 0; i < 3; i++)
                delete_statements(pstmt->stmts[i]);
            delete_statements(pstmt->loopbody);
            break;
        case STMT_VAR:
            delete_statements(pstmt->initializer);
            break;
    }
    if (pstmt->expression)
        delete_expression(pstmt->expression);
    if (pstmt->condition)
        delete_expression(pstmt->condition);
    if (pstmt->value)
        delete_expression(pstmt->value);
    free(pstmt);
}

static void synchronize(parser *analyzer)
{
    analyzer->panicmode = false;

    token **tokens =  analyzer->scan.tokens;
    while (tokens[analyzer->current]->type != TOKEN_EOF) {
        if (tokens[analyzer->current]->type == TOKEN_SEMICOLON) return;

        switch (tokens[analyzer->current]->type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;

            default:
                ;
        }

        advance(analyzer);
    }
}

static void error_at(parser *analyzer, token *tok, const char *msg)
{
    if (analyzer->panicmode) return;
    analyzer->panicmode = true;

    fprintf(stderr, "[line %d] Error", tok->line);

    if (tok->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (tok->type == TOKEN_ERROR)
        fprintf(stderr, ": %.*s", tok->length, tok->start);
    else
        fprintf(stderr, " at '%.*s'", tok->length, tok->start);
    fprintf(stderr, "\n");
    analyzer->haderror = true;
}

static void error(parser *analyzer, const char *msg)
{
    error_at(analyzer, analyzer->scan.tokens[analyzer->current - 1], msg);
}

static void error_at_current(parser *analyzer, const char *msg)
{
    error_at(analyzer, analyzer->scan.tokens[analyzer->current], msg);
}

static token *peek(parser *analyzer)
{
    return analyzer->scan.tokens[analyzer->current];
}

static bool is_at_end(parser *analyzer)
{
    return peek(analyzer)->type == TOKEN_EOF;
}

static bool check(parser *analyzer, tokentype type)
{
    if (is_at_end(analyzer))
        return false;
    return peek(analyzer)->type == type;
}

static token *previous(parser *analyzer)
{
    return analyzer->scan.tokens[analyzer->current - 1];
}

static token *advance(parser *analyzer)
{
    if (!is_at_end(analyzer))
        analyzer->current++;
    return previous(analyzer);
}

static token *consume(parser *analyzer, tokentype type, char *msg)
{
    if (check(analyzer, type))
        return advance(analyzer);

    error_at_current(analyzer, msg);
    return NULL;
}

static bool match(parser *analyzer, tokentype type)
{
    if (check(analyzer, type)) {
        advance(analyzer);
        return true;
    }
    return false;
}

static stmt *get_variable_statement(token *name, stmt *initializer)
{
    stmt *new_stmt = init_stmt();
    new_stmt->type = STMT_VAR;
    new_stmt->name = name;
    new_stmt->initializer = initializer;
    return new_stmt;
}

static stmt *get_expression_statement(expr *new_expr)
{
    stmt *new_stmt = init_stmt();
    new_stmt->type = STMT_EXPR;
    new_stmt->expression = new_expr;
    return new_stmt;
}

static stmt *get_for_statement(stmt *initializer, stmt *condition,
        stmt *iterator, stmt *loopbody)
{
    stmt *new_stmt = init_stmt();
    new_stmt->stmts = ALLOCATE(stmt*, 3);
    new_stmt->capacity = 3;
    new_stmt->stmts[0] = initializer;
    new_stmt->stmts[1] = condition;
    new_stmt->stmts[2] = iterator;
    new_stmt->count = 3;
    new_stmt->type = STMT_FOR;
    new_stmt->loopbody = loopbody;
    return new_stmt;
}

static stmt *get_while_statement(expr *condition, stmt *loopbody)
{
    stmt *new_stmt = init_stmt();
    new_stmt->type = STMT_WHILE;
    new_stmt->condition = condition;
    new_stmt->loopbody = loopbody;
    return new_stmt;
}

static stmt *get_if_statement(expr *condition, stmt *thenbranch, 
        stmt *elsebranch)
{
    stmt *new_stmt = init_stmt();
    new_stmt->type = STMT_IF;
    new_stmt->condition = condition;
    new_stmt->thenbranch = thenbranch;
    new_stmt->elsebranch = elsebranch;
    return new_stmt;
}

static expr *get_binary_expr(token *operator, expr *left, expr *right)
{
    expr *new_expr = init_expr();
    new_expr->type = EXPR_BINARY;
    new_expr->left = left;
    new_expr->right = right;
    new_expr->operator = operator;
    return new_expr;
}

static expr *get_unary_expr(token *opcode, expr *right)
{
    expr *new_expr = init_expr();
    new_expr->type = EXPR_UNARY;
    new_expr->operator = opcode;
    new_expr->right = right;
    return new_expr;
}

static expr *get_variable_expr(token *name)
{
    expr *new_expr = init_expr();
    new_expr->type = EXPR_VARIABLE;
    new_expr->name = name;
    return new_expr;
}

static expr *get_literal_expr(char *value, exprtype type)
{
    expr *new_expr = init_expr();
    new_expr->type = type;
    new_expr->literal = value;
    return new_expr;
}

static expr *get_grouping_expr(expr *group_expr)
{
    expr *new_expr = init_expr();
    new_expr->type = EXPR_GROUPING;
    new_expr->expression = group_expr;
    return new_expr;
}

static expr *get_assign_expr(token *name, expr *value)
{
    expr *new_expr = init_expr();
    new_expr->name = name;
    new_expr->value = value;
    new_expr->type = EXPR_ASSIGN;
    return new_expr;
}

static expr *primary(parser *analyzer)
{
    if (match(analyzer, TOKEN_FALSE)) {
        char *buffer = ALLOCATE(char, 2);
        char *number = "0";
        strcpy(buffer, number);
        return get_literal_expr(buffer, EXPR_LITERAL_BOOL);
    }
    if (match(analyzer, TOKEN_TRUE)) {
        char *buffer = ALLOCATE(char, 2);
        char *number = "1";
        strcpy(buffer, number);
        return get_literal_expr(buffer, EXPR_LITERAL_BOOL);
    }
    if (match(analyzer, TOKEN_NULL)) {
        char *buffer = ALLOCATE(char, 5);
        char *null = "NULL";
        strcpy(buffer, null);
        return get_literal_expr(buffer, EXPR_LITERAL_NULL);
    }
    if (match(analyzer, TOKEN_NUMBER)) {
        return get_literal_expr(take_string(previous(analyzer)),
                EXPR_LITERAL_NUMBER);
    }
    if (match(analyzer, TOKEN_STRING)) 
        return get_literal_expr(take_string(previous(analyzer)),
                    EXPR_LITERAL_STRING);
    if (match(analyzer, TOKEN_IDENTIFIER))
        return get_variable_expr(previous(analyzer));
    if (match(analyzer, TOKEN_LEFT_PAREN)) {
        expr *new_expr = expression(analyzer);
        consume(analyzer, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        return get_grouping_expr(new_expr);
    }

    error(analyzer, "Expect expression.");
    return NULL;
}

static expr *unary(parser *analyzer)
{
    if (match(analyzer, TOKEN_BANG) || match(analyzer, TOKEN_MINUS)) {
        token *opcode = previous(analyzer);
        expr *right = unary(analyzer);
        return get_unary_expr(opcode, right);
    }
    expr *literal = primary(analyzer);
    return literal;
}

static expr *multiplication(parser *analyzer)
{
    expr *new_expr = unary(analyzer);

    while (match(analyzer, TOKEN_SLASH) || match(analyzer, TOKEN_STAR)) {
        token *opcode = previous(analyzer);
        expr *right = unary(analyzer);
        new_expr = get_binary_expr(opcode, new_expr,
                right);
    }
    return new_expr;
}

static expr *addition(parser *analyzer)
{
    expr *new_expr = multiplication(analyzer);

    while (match(analyzer, TOKEN_MINUS) || match(analyzer, TOKEN_PLUS)) {
        token *opcode = previous(analyzer);
        expr *right = multiplication(analyzer);
        new_expr = get_binary_expr(opcode, new_expr,
                right);
    }
    return new_expr;
}

static expr *comparison(parser *analyzer)
{
    expr *new_expr = addition(analyzer);
    
    while (match(analyzer, TOKEN_GREATER) ||
            match(analyzer, TOKEN_GREATER_EQUAL) ||
            match(analyzer, TOKEN_LESS) ||
            match(analyzer, TOKEN_LESS_EQUAL)) {
        token *opcode = previous(analyzer);
        expr *right = addition(analyzer);
        new_expr = get_binary_expr(opcode, new_expr,
                right);
    }
    return new_expr;
}

static expr *equality(parser *analyzer)
{
    expr *new_expr = comparison(analyzer);
    while (match(analyzer, TOKEN_BANG_EQUAL)
            || match(analyzer, TOKEN_EQUAL_EQUAL)) {
        token *opcode = previous(analyzer);
        expr *right = comparison(analyzer);
        new_expr = get_binary_expr(opcode, new_expr,
                right);
    }
    return new_expr;
}

static expr* assignment(parser *analyzer)
{
    expr *new_expr = equality(analyzer);
    if (match(analyzer, TOKEN_EQUAL)) {
        if (new_expr->type == EXPR_VARIABLE)
            return get_assign_expr(new_expr->name, assignment(analyzer));
        error(analyzer, "Invalid assignment target.");
    }
    return new_expr;
}

static expr *expression(parser *analyzer)
{
    return assignment(analyzer);
}

static stmt *var_declaration(parser *analyzer)
{
    token *name = consume(analyzer, TOKEN_IDENTIFIER, "Expect variable name.");
    stmt *initializer = ALLOCATE(stmt, 1);

    if (match(analyzer, TOKEN_EQUAL))
        initializer = get_expression_statement(expression(analyzer));
    consume(analyzer, TOKEN_SEMICOLON, "Exepct ';' after variable declaration.");
    return get_variable_statement(name, initializer);
}

static stmt *declaration(parser *analyzer)
{
    if (match(analyzer, TOKEN_VAR))
        return var_declaration(analyzer);
    if (analyzer->panicmode) synchronize(analyzer);
    return statement(analyzer);
}

static inline void check_stmt_capacity(stmt *block_stmt)
{
    if (block_stmt->count == block_stmt->capacity - 1) {
        block_stmt->capacity *= 2;
        block_stmt->stmts = realloc(block_stmt->stmts, sizeof(stmt) * block_stmt->capacity);
    }
}

static stmt *block(parser *analyzer)
{
    stmt *new_stmt = ALLOCATE(stmt, 1);
    new_stmt->type = STMT_BLOCK;

    int i = 0;
    while (!check(analyzer, TOKEN_RIGHT_BRACE) && !is_at_end(analyzer)) {
        check_stmt_capacity(new_stmt);
        new_stmt->stmts[i++] = declaration(analyzer);
        new_stmt->count++;
    }

    consume(analyzer, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
    return new_stmt;
}

static stmt *if_statement(parser *analyzer)
{
    consume(analyzer, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expr *condition = expression(analyzer);
    consume(analyzer, TOKEN_RIGHT_PAREN, "Expect ')' after if condition.");

    stmt *then_branch = statement(analyzer);
    stmt *else_branch;
    if (match(analyzer, TOKEN_ELSE))
        else_branch = statement(analyzer);

    return get_if_statement(condition, then_branch, else_branch);
}

static stmt *for_statement(parser *analyzer)
{
    consume(analyzer, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    stmt *initializer = statement(analyzer);
    stmt *condition = statement(analyzer);
    stmt *iterator = statement(analyzer);
    consume(analyzer, TOKEN_RIGHT_PAREN, "Expect ')' after for condition.");
    stmt *loop_body = statement(analyzer);
    return get_for_statement(initializer, condition, iterator, loop_body);
}

static stmt *while_statement(parser *analyzer)
{
    consume(analyzer, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expr *condition = expression(analyzer);
    consume(analyzer, TOKEN_RIGHT_PAREN,
            "Expect ')' after while condition.");
    stmt *loop_body = statement(analyzer);
    return get_while_statement(condition, loop_body);
}

static stmt *expression_statement(parser *analyzer)
{
    expr *new_expr = expression(analyzer);
    consume(analyzer, TOKEN_SEMICOLON, "Expect ';' after expression.");
    return get_expression_statement(new_expr);
}

static stmt *statement(parser *analyzer)
{
    if (match(analyzer, TOKEN_FOR))
        return for_statement(analyzer);
    if (match(analyzer, TOKEN_WHILE))
        return while_statement(analyzer);
    if (match(analyzer, TOKEN_IF))
        return if_statement(analyzer);
    if (match(analyzer, TOKEN_LEFT_BRACE))
        return block(analyzer);
    return expression_statement(analyzer);
}

void reset_parser(parser *analyzer)
{
    if (analyzer->statements) {
        stmt **statements = analyzer->statements;
        for (int i = 0; i < analyzer->num_statements; ++i) {
            delete_statements(statements[i]);
        }
        free(analyzer->statements);
    }
    init_parser(analyzer);
}

void init_parser(parser *analyzer)
{
    analyzer->num_statements = 0;
    analyzer->capacity = 0;
    analyzer->statements = NULL;
    analyzer->current = 0;
    analyzer->num_tokens = 0;
    analyzer->panicmode = false;
    analyzer->haderror = false;
}

bool parse(parser *analyzer, const char *source)
{
    // Get the tokens
    init_scanner(&analyzer->scan);
    scan_tokens(&analyzer->scan, source);

    // Parse tokens into AST
    analyzer->capacity = GROW_CAPACITY(analyzer->capacity);
    analyzer->statements = GROW_ARRAY(analyzer->statements, stmt*, 0, analyzer->capacity); 

    int i = 0;
    while (!is_at_end(analyzer)) {
        if (analyzer->capacity < analyzer->num_statements + 1) {
            int oldcapacity = analyzer->capacity;
            analyzer->capacity = GROW_CAPACITY(analyzer->capacity);
            analyzer->statements = GROW_ARRAY(analyzer->statements, 
                    stmt*, oldcapacity, analyzer->capacity);
        }
        analyzer->statements[i] = declaration(analyzer);
        analyzer->num_statements++;
        if (analyzer->panicmode) synchronize(analyzer);
    }
    is_at_end(analyzer);
    return analyzer->haderror;
}