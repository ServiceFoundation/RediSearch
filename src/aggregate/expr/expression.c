#include "expression.h"
#include "result_processor.h"
RSArgList *RS_NewArgList(RSExpr *e) {
  RSArgList *ret = malloc(sizeof(*ret) + (e ? 1 : 0) * sizeof(RSExpr *));
  ret->len = e ? 1 : 0;
  if (e) ret->args[0] = e;
}

static RSExpr *newExpr(RSExprType t) {
  RSExpr *e = malloc(sizeof(e));
  e->t = t;
  return e;
}
RSExpr *RS_NewStringLiteral(char *str, size_t len) {

  RSExpr *e = newExpr(RSExpr_Literal);
  e->literal = RS_StaticValue(RSValue_String);
  e->literal.strval.len = len;
  e->literal.strval.str = strndup(str, len);
  return e;
}

RSExpr *RS_NewNumberLiteral(double n) {
  RSExpr *e = newExpr(RSExpr_Literal);

  e->literal = RS_StaticValue(RSValue_Number);
  e->literal.numval = n;
  return e;
}

RSExpr *RS_NewOp(unsigned char op, RSExpr *left, RSExpr *right) {
  RSExpr *e = newExpr(RSExpr_Op);
  e->op.op = op;
  e->op.left = left;
  e->op.right = right;
  return e;
}

RSExpr *RS_NewFunc(char *str, size_t len, RSArgList *args) {
  RSExpr *e = newExpr(RSExpr_Function);
  e->func.args = args;
  e->func.name = strndup(str, len);
  // TODO: validate name and func
  return e;
}
RSExpr *RS_NewProp(char *str, size_t len) {
  RSExpr *e = newExpr(RSExpr_Property);
  e->property.key = strndup(str, len);
  e->property.cachedIdx = RSKEY_UNCACHED;
  return e;
}
void RSArgList_Free(RSArgList *l) {
  if (!l) return;
  for (size_t i = 0; i < l->len; i++) {
    RSExpr_Free(l->args[i]);
  }
  free(l);
}
void RSExpr_Free(RSExpr *e) {
  switch (e->t) {
    case RSExpr_Literal:
      RSValue_Free(&e->literal);
      break;
    case RSExpr_Function:
      free((char *)e->func.name);
      RSArgList_Free(e->func.args);
      break;
    case RSExpr_Op:
      RSExpr_Free(e->op.left);
      RSExpr_Free(e->op.right);
      break;
    case RSExpr_Property:
      free((char *)e->property.key);
  }
}

static int evalFunc(RSExprEvalCtx *ctx, RSFunction *f, RSValue *result, char **err) {
  RSFunctionCallback cb = RSFunctionRegistry_Get(f->name);
  if (!cb) {
    asprintf(err, "Could not find function '%s'", f->name);
    return EXPR_EVAL_ERR;
  }

  RSValue args[f->args->len];
  for (size_t i = 0; i < f->args->len; i++) {
    if (RSExpr_Eval(ctx, f->args->args[i], &args[i], err) == EXPR_EVAL_ERR) {
      // TODO: Free other results
      return EXPR_EVAL_ERR;
    }
  }

  int rc = cb(ctx->r, args, f->args->len, NULL);
  for (size_t i = 0; i < f->args->len; i++) {
    RSValue_Free(&args[i]);
  }
  return rc;
}

static int evalOp(RSExprEvalCtx *ctx, RSExprOp *op, RSValue *result, char **err) {

  RSValue l, r;
  if (RSExpr_Eval(ctx, op->left, &l, err) == EXPR_EVAL_ERR) {
    return EXPR_EVAL_ERR;
  }
  if (RSExpr_Eval(ctx, op->left, &r, err) == EXPR_EVAL_ERR) {
    return EXPR_EVAL_ERR;
  }

  double n1, n2;
  int rc = EXPR_EVAL_OK;
  if (!RSValue_ToNumber(&l, &n1) || !RSValue_ToNumber(&r, &n2)) {
    asprintf(err, "Invalid values for op '%c'", op->op);
    rc = EXPR_EVAL_ERR;
    goto cleanup;
  }

  double res;
  switch (op->op) {
    case '+':
      res = n1 + n2;
      break;
    case '/':
      res = n1 / n2;
      break;
    case '-':
      res = n1 - n2;
      break;
    case '*':
      res = n1 * n2;
      break;
    case '%':
      res = (long long)n1 % (long long)n2;
      break;
    case '^':
      res = pow(n1, n2);
      break;
  }

  result->refcount = 1;
  result->numval = res;
  result->t = RSValue_Number;

cleanup:
  RSValue_Free(&l);
  RSValue_Free(&r);
  return rc;
}

static int evalProperty(RSExprEvalCtx *ctx, RSKey *k, RSValue *result, char **err) {
  RSValue_MakeReference(result, SearchResult_GetValue(ctx->r, ctx->sortables, k));
  return EXPR_EVAL_OK;
}

int RSExpr_Eval(RSExprEvalCtx *ctx, RSExpr *e, RSValue *result, char **err) {
  switch (e->t) {
    case RSExpr_Function:
      return evalFunc(ctx, &e->func, result, err);
    case RSExpr_Literal:
      RSValue_MakeReference(result, &e->literal);
      return 1;
    case RSExpr_Op:
      return evalOp(ctx, &e->op, result, err);
    case RSExpr_Property:
      return evalProperty(ctx, &e->property, result, err);
  }
}