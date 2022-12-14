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

#include "sql/ob_sql_temp_table.h"

using namespace oceanbase::sql;

OB_SERIALIZE_MEMBER(ObTempTableResultInfo,
                    addr_,
                    interm_result_ids_);

OB_SERIALIZE_MEMBER(ObSqlTempTableCtx,
                    interm_result_infos_,
                    temp_table_id_,
                    is_local_interm_result_);
