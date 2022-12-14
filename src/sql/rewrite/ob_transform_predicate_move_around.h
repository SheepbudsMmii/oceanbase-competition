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

#ifndef OB_TRANSFORM_PREDICATE_MOVE_AROUND_H
#define OB_TRANSFORM_PREDICATE_MOVE_AROUND_H

#include "sql/rewrite/ob_transform_rule.h"
#include "sql/resolver/dml/ob_select_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObDelUpdStmt;
class ObTransformPredicateMoveAround : public ObTransformRule
{
public:
  ObTransformPredicateMoveAround(ObTransformerCtx *ctx);

  virtual ~ObTransformPredicateMoveAround();

  virtual int transform_one_stmt(ObIArray<ObParentDMLStmt> &parent_stmts,
                                 ObDMLStmt *&stmt,
                                 bool &trans_happened) override;

  virtual int transform_one_stmt_with_outline(ObIArray<ObParentDMLStmt> &parent_stmts,
                                              ObDMLStmt *&stmt,
                                              bool &trans_happened) override;

  virtual int construct_transform_hint(ObDMLStmt &stmt, void *trans_params) override;
private:

  virtual int need_transform(const common::ObIArray<ObParentDMLStmt> &parent_stmts,
                             const int64_t current_level,
                             const ObDMLStmt &stmt,
                             bool &need_trans) override;

  int check_outline_valid_to_transform(const ObDMLStmt &stmt, bool &need_trans);

  int pullup_predicates(ObDMLStmt *stmt,
                        ObIArray<int64_t> &select_list,
                        ObIArray<ObRawExpr *> &properties);

  int preprocess(ObDMLStmt &stmt);

  int preprocess_semi_info(ObDMLStmt &stmt,
                           SemiInfo *semi_info,
                           ObIArray<ObRawExpr*> &upper_conds);
  int preprocess_joined_table(ObDMLStmt &stmt, 
                              JoinedTable *join_table,
                              ObIArray<ObRawExpr*> &upper_conds);

  int pullup_predicates_from_view(ObDMLStmt &stmt,
                                  ObIArray<int64_t> &sel_ids,
                                  ObIArray<ObRawExpr *> &input_pullup_preds);

  int generate_set_pullup_predicates(ObSelectStmt &stmt,
                                    ObIArray<int64_t> &sel_ids,
                                    ObIArray<ObRawExpr *> &input_pullup_preds,
                                    ObIArray<ObRawExpr *> &output_pullup_preds);

  /**
   * @brief pullup_predicates_from_set
   * set stmt?????????????????????????????????
   * @param stmt
   * @param pullup_preds ???????????????
   * @param parent_stmt set stmt?????????stmt???????????????????????????
   */
  int pullup_predicates_from_set(ObSelectStmt *stmt,
                                ObIArray<ObRawExpr *> &pullup_preds);
  
  /**
   * @brief check_pullup_predicates
   * ??????set op?????????????????????????????????
   * union????????????????????????
   * interset???????????????????????????
   * except????????????????????????
   */
  int check_pullup_predicates(ObSelectStmt *stmt,
                              ObIArray<ObRawExpr *> &left_pullup_preds,
                              ObIArray<ObRawExpr *> &right_pullup_preds,
                              ObIArray<ObRawExpr *> &output_pullup_preds);

  int generate_pullup_predicates(ObSelectStmt &select_stmt,
                                 ObIArray<int64_t> &sel_ids,
                                 ObIArray<ObRawExpr *> &input_pullup_preds,
                                 ObIArray<ObRawExpr *> &output_pullup_preds);

  int remove_pullup_union_predicates(ObIArray<ObRawExpr *> &exprs);

  int remove_useless_equal_const_preds(ObSelectStmt *stmt,
                                       ObIArray<ObRawExpr *> &exprs,
                                       ObIArray<ObRawExpr *> &equal_const_preds);

  int choose_pullup_columns(TableItem &table,
                            ObIArray<ObRawExpr *> &columns,
                            ObIArray<int64_t> &view_sel_list);

  int compute_pullup_predicates(ObSelectStmt &view,
                                const ObIArray<int64_t> &select_list,
                                ObIArray<ObRawExpr *> &local_preds,
                                ObIArray<ObRawExpr *> &pull_up_preds);

  int check_expr_pullup_validity(const ObRawExpr *expr,
                                 const ObIArray<ObRawExpr *> &pullup_list,
                                 int64_t &state);

  int rename_pullup_predicates(ObDMLStmt &stmt,
                               TableItem &view,
                               const ObIArray<int64_t> &view_sel_list,
                               ObIArray<ObRawExpr *> &preds);

  int pullup_predicates_from_const_select(ObSelectStmt *parent_stmt,
                                          ObSelectStmt *child_stmt,
                                          ObIArray<ObRawExpr*> &pullup_preds);

  int pushdown_predicates(ObDMLStmt *stmt,
                          ObIArray<ObRawExpr *> &predicates);

  /**
   * @brief pushdown_into_set_stmt
   * ???????????????set stmt?????????????????????
   * @param stmt
   * @param predicates ??????????????????
   */
  int pushdown_into_set_stmt(ObSelectStmt *stmt,
                            ObIArray<ObRawExpr *> &pullup_preds,
                            ObIArray<ObRawExpr *> &pushdown_preds);

  /**
   * @brief check_pushdown_predicates
   * ????????????????????????????????????????????????????????????????????????stmt
   * union?????????????????????????????????????????????union????????????????????????????????????
   * intersect????????????????????????????????????????????????intersect?????????????????????????????????
   * except???????????????????????????????????????except??????????????????????????????
   */
  int check_pushdown_predicates(ObSelectStmt *stmt,
                                ObIArray<ObRawExpr *> &left_pushdown_preds,
                                ObIArray<ObRawExpr *> &right_pushdown_preds,
                                ObIArray<ObRawExpr *> &output_pushdown_preds);

  int extract_valid_preds(ObIArray<ObRawExpr *> &all_preds,
                          ObIArray<ObRawExpr *> &valid_exprs,
                          ObIArray<ObRawExpr *> &invalid_exprs);

  /**
   * @brief pushdown_into_set_stmt
   * ???????????????set stmt???????????????
   * @param stmt
   * @param predicates ??????????????????
   * @param ????????????????????????????????????????????????
   */
  int pushdown_into_set_stmt(ObSelectStmt *stmt,
                            ObIArray<ObRawExpr *> &pullup_preds,
                            ObIArray<ObRawExpr *> &pushdown_preds,
                            ObSelectStmt *parent_stmt);

  /**
   * @brief rename_set_op_predicates
   * ???????????????
   * @param child_stmt 
   * @param parent_stmt
   * @param preds ??????????????????
   * @param is_pullup
   * ?????????pullup?????????????????????child_stmt?????????parent_stmt
   * ?????????pushdown?????????????????????parent_stmt?????????child_stmt
   */
  int rename_set_op_predicates(ObSelectStmt &child_stmt,
                              ObSelectStmt &parent_stmt,
                              ObIArray<ObRawExpr *> &preds,
                              bool is_pullup);

  int pushdown_into_having(ObSelectStmt &stmt,
                           ObIArray<ObRawExpr *> &pullup_preds,
                           ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_into_where(ObDMLStmt &stmt,
                          ObIArray<ObRawExpr *> &pullup_preds,
                          ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_into_semi_info(ObDMLStmt *stmt,
                              SemiInfo *semi_info,
                              ObIArray<ObRawExpr *> &pullup_preds,
                              ObIArray<ObRawExpr *> &pushdown_preds);

  int pushdown_semi_info_right_filter(ObDMLStmt *stmt,
                                      ObTransformerCtx *ctx,
                                      SemiInfo *semi_info);
  
  int check_has_shared_query_ref(ObRawExpr *expr, bool &has);
  
  int extract_semi_right_table_filter(ObDMLStmt *stmt,
                                      SemiInfo *semi_info,
                                      ObIArray<ObRawExpr *> &right_filters);
  int pushdown_into_table(ObDMLStmt *stmt,
                          TableItem *table,
                          ObIArray<ObRawExpr *> &pullup_preds,
                          ObIArray<ObRawExpr *> &preds);

  int get_pushdown_predicates(ObDMLStmt &stmt,
                              TableItem &table,
                              ObIArray<ObRawExpr *> &preds,
                              ObIArray<ObRawExpr *> &table_filters);

  int get_pushdown_predicates(ObDMLStmt &stmt,
                              ObSqlBitSet<> &table_set,
                              ObIArray<ObRawExpr *> &preds,
                              ObIArray<ObRawExpr *> &table_filters);

  int pushdown_into_joined_table(ObDMLStmt *stmt,
                                 JoinedTable *joined_table,
                                 ObIArray<ObRawExpr *> &pullup_preds,
                                 ObIArray<ObRawExpr *> &pushdown_preds);

  int check_transform_happened(ObDMLStmt *stmt,
                               ObIArray<ObRawExpr *> &old_conditions,
                               ObIArray<ObRawExpr *> &new_conditions);

  int pushdown_through_winfunc(ObSelectStmt &stmt,
                               ObIArray<ObRawExpr *> &predicates,
                               ObIArray<ObRawExpr *> &down_preds);

  int pushdown_through_groupby(ObSelectStmt &stmt,
                               ObIArray<ObRawExpr *> &output_predicates);

  int deduce_param_cond_from_aggr_cond(ObItemType expr_type,
                                       ObRawExpr *first,
                                       ObRawExpr *second,
                                       ObRawExpr *&new_predicate);

  int split_or_having_expr(ObSelectStmt &stmt,
                          ObOpRawExpr &or_qual,
                          ObRawExpr *&new_expr);

  int check_having_expr(ObSelectStmt &stmt,
                        ObOpRawExpr &or_qual,
                        ObIArray<ObSEArray<ObRawExpr *, 16> > &sub_exprs,
                        bool &all_contain);

  int inner_split_or_having_expr(ObSelectStmt &stmt,
                                ObIArray<ObSEArray<ObRawExpr *, 16> > &sub_exprs,
                                ObRawExpr *&new_expr);                                      

  int choose_pushdown_preds(ObIArray<ObRawExpr *> &preds,
                            ObIArray<ObRawExpr *> &invalid_preds,
                            ObIArray<ObRawExpr *> &valid_preds);

  int rename_pushdown_predicates(ObDMLStmt &stmt,
                                 TableItem &view,
                                 ObIArray<ObRawExpr *> &preds);

  int transform_predicates(ObDMLStmt &stmt,
                           ObIArray<ObRawExpr *> &input_preds,
                           ObIArray<ObRawExpr *> &target_exprs,
                           ObIArray<ObRawExpr *> &output_preds,
                           bool is_pullup = false);

  int accept_predicates(ObDMLStmt &stmt,
                        ObIArray<ObRawExpr *> &conds,
                        ObIArray<ObRawExpr *> &properties,
                        ObIArray<ObRawExpr *> &new_conds);

  int extract_generalized_column(ObRawExpr *expr,
                                 const int32_t expr_level,
                                 ObIArray<ObRawExpr *> &output);

  int acquire_transform_params(ObDMLStmt *stmt, ObIArray<ObRawExpr *> *&preds);

  int get_columns_in_filters(ObDMLStmt &stmt,
                             ObIArray<int64_t> &sel_items,
                             ObIArray<ObRawExpr *> &columns);

  int create_equal_exprs_for_insert(ObDelUpdStmt *del_upd_stmt, bool &is_happened);

  int print_debug_info(const char *str, ObDMLStmt *stmt, ObIArray<ObRawExpr *> &preds);

  int generate_pullup_predicates_for_dual_stmt(ObDMLStmt &stmt,
                                               TableItem &view,
                                               const ObIArray<int64_t> &sel_ids,
                                               ObIArray<ObRawExpr *> &preds);

  int check_false_condition(ObSelectStmt *stmt, bool &false_cond_exists);

  int check_enable_no_pred_deduce(ObDMLStmt &stmt, bool &enable_no_pred_deduce);

  int get_stmt_to_trans(ObDMLStmt *stmt, ObIArray<ObDMLStmt *> &stmt_to_trans);

private:
  typedef ObSEArray<ObRawExpr *, 4> PullupPreds;
  ObArenaAllocator allocator_;
  hash::ObHashMap<uint64_t, int64_t> stmt_map_;
  Ob2DArray<PullupPreds *> stmt_pullup_preds_;
  ObSEArray<ObDMLStmt *, 8> transed_stmts_;
  ObSEArray<ObHint *, 4> applied_hints_;
};

}
}

#endif // OB_TRANSFORM_PREDICATE_MOVE_AROUND_H
