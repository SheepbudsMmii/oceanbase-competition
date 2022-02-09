/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

// This file is for define of func json_quote
#ifndef OCEANBASE_SQL_OB_EXPR_JSON_QUOTE_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_QUOTE_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/json_type/ob_json_tree.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExprJsonQuote : public ObFuncExprOperator
{
public:
  explicit ObExprJsonQuote(common::ObIAllocator &alloc);
  virtual ~ObExprJsonQuote();
  template <typename T>
  static int calc(const T &data, ObObjType type, ObCollationType cs_type,
                  ObJsonBuffer &j_buf, bool &is_null);
  int calc_result1(common::ObObj &result, const common::ObObj &obj, common::ObExprCtx &expr_ctx) const override;
  int calc_result_type1(ObExprResType &type,
                        ObExprResType &type1,
                        common::ObExprTypeCtx &type_ctx) const override;

  static int eval_json_quote(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprJsonQuote);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_QUOTE_H_