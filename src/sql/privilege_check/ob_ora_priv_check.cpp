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

#define USING_LOG_PREFIX SQL_SESSION
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/privilege_check/ob_ora_priv_check.h"
#include "share/schema/ob_sys_priv_type.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/expr/ob_expr_user_can_access_obj.h"

namespace oceanbase {

using namespace share;
using namespace schema;
using namespace common;

namespace sql {

/* don't use grantor_id in obj_key */
int ObOraSysChecker::get_roles_obj_privs(
    ObSchemaGetterGuard &guard,
    const ObObjPrivSortKey &obj_key, 
    ObPackedObjPriv &obj_privs,
    const bool fetch_public_role_flag,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = obj_key.tenant_id_;
  uint64_t user_id = obj_key.grantee_id_;
  uint64_t role_id;
  const ObUserInfo *user_info = NULL;
  UNUSED(fetch_public_role_flag);
  obj_privs = 0;

  if (OB_FAIL(guard.get_user_info(tenant_id, user_id, user_info))) {
    LOG_WARN("failed to get user info", K(ret), K(tenant_id), K(user_id));
  } else if (NULL == user_info) {
    ret = OB_USER_NOT_EXIST;
    LOG_WARN("user info is null", K(ret), K(user_id));
  } else {
    for (int i = 0; OB_SUCC(ret) && i < role_id_array.count(); ++i) {
      ObObjPrivSortKey role_obj_key = obj_key;
      role_id = role_id_array.at(i);

      role_obj_key.grantee_id_ = role_id;
      ObPackedObjPriv tmp_privs = 0;
      
      /* merge privs of roles */
      OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, 
                                            role_obj_key,
                                            tmp_privs));
      OX (obj_privs |= tmp_privs);
      
    }
  }
  return ret;
}

int ObOraSysChecker::get_user_sys_priv_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id, 
    ObPackedPrivArray &packed_array_out,
    const bool fetch_public_role_flag,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  uint64_t role_id;
  const ObUserInfo *user_info = NULL;
  UNUSED(fetch_public_role_flag);
  packed_array_out.reset();
  if (OB_FAIL(guard.get_user_info(tenant_id, user_id, user_info))) {
    LOG_WARN("failed to get user info", K(ret), K(tenant_id), K(user_id));
  } else if (NULL == user_info) {
      ret = OB_USER_NOT_EXIST;
      LOG_WARN("user info is null", K(ret), K(user_id));
  } else {
    for (int i = 0; OB_SUCC(ret) && i < role_id_array.count(); ++i) {
      ObSysPriv *tmp_sys_priv_p;
      ObPackedPrivArray tmp_sys_priv_array;
      role_id = role_id_array.at(i);

      OZ (guard.get_sys_priv_with_grantee_id(tenant_id, 
                                             role_id,
                                             tmp_sys_priv_p));
      if (OB_SUCC(ret) && tmp_sys_priv_p != NULL) {
        OZ (ObPrivPacker::merge_two_packed_array(packed_array_out, 
                                                 tmp_sys_priv_p->get_priv_array()), 
            packed_array_out,
            tmp_sys_priv_p->get_priv_array());
      }
    }
  }
  return ret;         
}

int ObOraSysChecker::check_single_sys_priv_inner(
    const ObPackedPrivArray& sys_packed_array,
    const ObRawPriv p1)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  OZ (ObOraPrivCheck::raw_sys_priv_exists(p1, sys_packed_array, exists));
  if (OB_SUCC(ret) && !exists) {
    ret = OB_ERR_NO_PRIVILEGE;
  }
  return ret;
}    

int ObOraSysChecker::check_single_sys_priv_inner(
    const ObSysPriv *sys_priv,
    const ObRawPriv p1)
{
  int ret = OB_SUCCESS;
  if (sys_priv == NULL) {
    ret = OB_ERR_NO_PRIVILEGE;
  } else {
    OZX1 (check_single_sys_priv_inner(sys_priv->get_priv_array(), p1), OB_ERR_NO_PRIVILEGE);
  }
  return ret;
}

int ObOraSysChecker::check_p1_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1)
{
  int ret = OB_SUCCESS;
  ObSysPriv *sys_priv = NULL;
  
  OZ (guard.get_sys_priv_with_grantee_id(tenant_id, user_id, sys_priv));   
  OZX1 (check_single_sys_priv_inner(sys_priv, p1), OB_ERR_NO_PRIVILEGE);
  return ret;
}

int ObOraSysChecker::check_p1_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObPackedPrivArray sys_packed_array;
  OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array,
                                    true, role_id_array),
      tenant_id, user_id);
  OZX1 (check_single_sys_priv_inner(sys_packed_array, p1), OB_ERR_NO_PRIVILEGE);
  return ret;
}

/* check user ????????????????????????????????????
  1. ???check ??????????????????
  2. ??????check??????role????????????????????? */
int ObOraSysChecker::check_plist_and(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX1 (check_plist_and_in_single(guard, tenant_id, user_id, plist, option), OB_ERR_NO_PRIVILEGE);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZX1 (check_plist_and_in_roles(guard, tenant_id, user_id, plist, role_id_array, option),
          OB_ERR_NO_PRIVILEGE, tenant_id, user_id, plist);
    }
  }
  return ret;  
}

/* check user ???????????????????????????????????????
  1. ???check ??????????????????
  2. ??????check??????role????????????????????? */
int ObOraSysChecker::check_plist_or(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX1 (check_plist_or_in_single(guard, tenant_id, user_id, plist, option), OB_ERR_NO_PRIVILEGE);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZX1 (check_plist_or_in_roles(guard, tenant_id, user_id, plist, role_id_array, option),
          OB_ERR_NO_PRIVILEGE, tenant_id, user_id, plist);
    }
  }
  return ret;  
}

/* check user ???????????????????????????
  1. ???check ??????????????????
  2. ??????check??????role????????????????????? */
int ObOraSysChecker::check_p1(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX1 (check_p1_in_single(guard, tenant_id, user_id, p1), OB_ERR_NO_PRIVILEGE);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZX1 (check_p1_in_roles(guard, tenant_id, user_id, p1, role_id_array),
          OB_ERR_NO_PRIVILEGE, tenant_id, user_id, p1);
    }
  }
  return ret;  
}

/* check user ????????????????????????????????????????????? */
int ObOraSysChecker::check_plist_or_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  bool exists;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    ObPackedPrivArray sys_packed_array;
    OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array,
                                   true, role_id_array),
      tenant_id, user_id);
    
    ObPackedPrivArray packed_privs;
    OZ (ObPrivPacker::pack_raw_priv_list(option, plist, packed_privs));
    OZ (ObOraPrivCheck::packed_sys_priv_list_or_exists(packed_privs, 
                                                       sys_packed_array,
                                                       exists));
    if (OB_SUCC(ret) && !exists) {
      ret = OB_ERR_NO_PRIVILEGE;
    }
  }
  return ret;  
}

/* check user ??????????????????????????? */
int ObOraSysChecker::check_plist_and_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  bool exists;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    ObPackedPrivArray sys_packed_array;
    OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array,
                                   true, role_id_array),
      tenant_id, user_id);
    
    ObPackedPrivArray packed_privs;
    OZ (ObPrivPacker::pack_raw_priv_list(option, plist, packed_privs));
    OZ (ObOraPrivCheck::packed_sys_priv_list_and_exists(packed_privs, 
                                                       sys_packed_array,
                                                       exists));
    if (OB_SUCC(ret) && !exists) {
      ret = OB_ERR_NO_PRIVILEGE;
    }
  }
  return ret;  
}

/* check user ????????????????????????????????????????????? */
int ObOraSysChecker::check_plist_and_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  bool exists;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    ObSysPriv *sys_priv = NULL;
    OZ (guard.get_sys_priv_with_grantee_id(tenant_id, user_id, sys_priv));
    if (OB_SUCC(ret)) {
      if (sys_priv == NULL) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else {
        ObPackedPrivArray packed_privs;
        OZ (ObPrivPacker::pack_raw_priv_list(option, plist, packed_privs));
        OZ (ObOraPrivCheck::packed_sys_priv_list_and_exists(packed_privs, 
                                                           sys_priv->get_priv_array(),
                                                           exists));
        if (OB_SUCC(ret) && !exists) {
          ret = OB_ERR_NO_PRIVILEGE;
        }
      }
    }
  }
  return ret;  
}

/* check user ????????????????????????????????????????????? */
int ObOraSysChecker::check_plist_or_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPrivArray &plist,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  bool exists;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    ObSysPriv *sys_priv = NULL;
    OZ (guard.get_sys_priv_with_grantee_id(tenant_id, user_id, sys_priv));
    if (OB_SUCC(ret)) {
      if (sys_priv == NULL) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else {
        ObPackedPrivArray packed_privs;
        OZ (ObPrivPacker::pack_raw_priv_list(option, plist, packed_privs));
        OZ (ObOraPrivCheck::packed_sys_priv_list_or_exists(packed_privs, 
                                                           sys_priv->get_priv_array(),
                                                           exists));
        if (OB_SUCC(ret) && !exists) {
          ret = OB_ERR_NO_PRIVILEGE;
        }
      }
    }
  }
  return ret;  
}

/* check user ???rolearray????????????????????????????????????????????? */
int ObOraSysChecker::check_obj_plist_or_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObObjPrivSortKey obj_key(tenant_id, obj_id, obj_type, col_id, user_id, user_id);
  ObPackedObjPriv tmp_privs = 0;
  ObPackedObjPriv packed_privs;
  bool exists = false;
  /* 1. get roles's privs --> tmp_privs */
  OZ (get_roles_obj_privs(guard, obj_key, tmp_privs, true, role_id_array));
  /* 2. get privs needed --> packed_privs */
  OZ (ObPrivPacker::pack_raw_obj_priv_list(NO_OPTION, plist, packed_privs));    
  /* 3. check */
  OZ (ObOraPrivCheck::packed_obj_priv_list_or_exists(packed_privs, 
                                                     tmp_privs,
                                                     exists));
  if (OB_SUCC(ret) && !exists) {
      ret = OB_ERR_NO_PRIVILEGE;
  }

  return ret;  
}

/* check user ????????????????????????????????????????????? */
int ObOraSysChecker::check_obj_plist_or_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPrivArray &plist)
{
  int ret = OB_SUCCESS;
  bool exists;
  ObObjPrivSortKey obj_key(tenant_id, obj_id, obj_type, col_id, user_id, user_id);
  ObPackedObjPriv tmp_privs = 0;
  OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, tmp_privs));
  if (OB_SUCC(ret)) {
    if (tmp_privs == 0) {
      ret = OB_ERR_EMPTY_QUERY;
    } else {
      ObPackedObjPriv packed_privs;
      OZ (ObPrivPacker::pack_raw_obj_priv_list(NO_OPTION, plist, packed_privs));
      OZ (ObOraPrivCheck::packed_obj_priv_list_or_exists(packed_privs, 
                                                         tmp_privs,
                                                         exists));
      if (OB_SUCC(ret) && !exists) {
          ret = OB_ERR_NO_PRIVILEGE;
      }
    }
  }
  return ret;  
}

int ObOraSysChecker::check_obj_p1_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPriv p1,
    const uint64_t option)
{
  int ret = OB_SUCCESS;
  ObObjPrivSortKey obj_key(tenant_id, obj_id, obj_type, col_id, user_id, user_id);
  ObPackedObjPriv tmp_privs = 0;
  OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, tmp_privs));
  OZX2 (check_single_option_obj_priv_inner(tmp_privs, option, p1), 
       OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY);
  return ret;
}

int ObOraSysChecker::check_obj_p1_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPriv p1,
    const uint64_t option,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObObjPrivSortKey obj_key(tenant_id, obj_id, obj_type, col_id, user_id, user_id);
  ObPackedObjPriv tmp_privs = 0;
  OZ (get_roles_obj_privs(guard, obj_key, tmp_privs, true, role_id_array));
  OZX1 (check_single_option_obj_priv_inner(tmp_privs, option, p1), OB_ERR_EMPTY_QUERY);
  return ret;
}

/* check user ???????????????????????????
  1. ???check ??????????????????
  2. ????????????????????????role??????????????? */
int ObOraSysChecker::check_obj_plist_or(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  lib::Worker::CompatMode compat_mode;
  int ret1;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX2 (check_obj_plist_or_in_single(guard, tenant_id, user_id, obj_type, 
                                       obj_id, col_id, plist),
         OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY,
         tenant_id, user_id, obj_type, obj_id, col_id, plist);
    if (ret == OB_ERR_NO_PRIVILEGE || ret == OB_ERR_EMPTY_QUERY) {
      ret1 = ret;
      ret = OB_SUCCESS;
      OZ (check_obj_plist_or_in_roles(guard, tenant_id, user_id, obj_type, 
          obj_id, col_id, plist, role_id_array),
          tenant_id, user_id, obj_type, obj_id, col_id, plist);
      /* ????????????????????????????????????????????????????????????????????? no privilege */
      if (ret == OB_ERR_EMPTY_QUERY && ret1 != OB_ERR_EMPTY_QUERY) {
        ret = OB_ERR_NO_PRIVILEGE;
      }
    }
  }
  return ret;
}

/* check user ???????????????????????????
  1. ???check ??????????????????
  2. ????????????????????????role??????????????? */
int ObOraSysChecker::check_obj_p1(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const uint64_t col_id,
    const ObRawObjPriv p1,
    const uint64_t option,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  // unused
  // bool exists;
  lib::Worker::CompatMode compat_mode;
  // unused
  // ObObjPriv *obj_priv = NULL;
  int ret1;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX2 (check_obj_p1_in_single(guard, tenant_id, user_id, obj_type, obj_id, col_id, p1, option),
         OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY,
         tenant_id, user_id, obj_type, obj_id, col_id, p1, option);
    if (ret == OB_ERR_NO_PRIVILEGE || ret == OB_ERR_EMPTY_QUERY) {
      ret1 = ret;
      ret = OB_SUCCESS;
      OZX1 (check_obj_p1_in_roles(guard, tenant_id, user_id, obj_type, 
          obj_id, col_id, p1, option, role_id_array), OB_ERR_EMPTY_QUERY,
          tenant_id, user_id, obj_type, obj_id, col_id, p1, option);
      /* ????????????????????????????????????????????????????????????????????? no privilege */
      if (ret == OB_ERR_EMPTY_QUERY && ret1 != OB_ERR_EMPTY_QUERY) {
        ret = OB_ERR_NO_PRIVILEGE;
      }
    }
  }
  return ret;
}

int ObOraSysChecker::check_p1_or_plist_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    const ObRawPrivArray &plist)
{
  int ret = OB_SUCCESS;
  ObSysPriv *sys_priv = NULL;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZ (guard.get_sys_priv_with_grantee_id(tenant_id, user_id, sys_priv));   
    if (OB_SUCC(ret)) {
      if (sys_priv == NULL) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else {
        OZX1 (check_p1_or_plist_using_privs(sys_priv->get_priv_array(), p1, option, plist),
                                           OB_ERR_NO_PRIVILEGE);
      }
    }
  }
  return ret;
}   

int ObOraSysChecker::check_p1_or_plist_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObPackedPrivArray sys_packed_array;
  OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array,
                                 true, role_id_array),
      tenant_id, user_id);
  OZ (check_p1_or_plist_using_privs(sys_packed_array, p1, option, plist));
  return ret;
}

/* check user ?????????p1?????????????????????????????????????????????plist???????????????????????????
  1. ???check ??????????????????
  2. ????????????????????????role??????????????? 
  3. ?????????p1??????????????????
     ????????????p1??? ?????????????????????????????????plist???????????????
  ???????????????????????????????????????
??????:
   select * from user1.table1;
   ?????????????????????????????????????????????????????????
   ????????????????????????????????????????????????????????????????????????table not exists
               ????????????????????????????????????????????????????????????????????????????????????????????????select any table??????
               ???????????????????????????create any table/alter any table/drop any table????????????    */
int ObOraSysChecker::check_p1_with_plist_info(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    ObRawPrivArray &plist,
    bool &has_other_priv,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  // unused
  // ObSysPriv *sys_priv = NULL;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX1 (check_p1_with_plist_info_in_single(
          guard, tenant_id, user_id, p1, option, plist, has_other_priv),
                                     OB_ERR_NO_PRIVILEGE);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      if (has_other_priv) {
        OZX1 (check_p1_in_roles(guard, tenant_id, user_id, p1, role_id_array), OB_ERR_NO_PRIVILEGE);
      } else {
        OZX1 (check_p1_with_plist_info_in_roles(
              guard, tenant_id, user_id, p1, option, plist, 
              has_other_priv, role_id_array), OB_ERR_NO_PRIVILEGE);
      }
    }
  }
  return ret;
}   

int ObOraSysChecker::check_p1_with_plist_info_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    ObRawPrivArray &plist,
    bool &has_other_priv,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObPackedPrivArray sys_packed_array;
  OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array, 
                                    true, role_id_array),
      tenant_id, user_id);
  OZX1 (check_p1_with_plist_info_using_privs(sys_packed_array, p1, option, plist, has_other_priv),
        OB_ERR_NO_PRIVILEGE);
  return ret;
}

int ObOraSysChecker::check_p1_with_plist_info_using_privs(
    const ObPackedPrivArray &priv_array,
    const ObRawPriv p1,
    const uint64_t option,
    ObRawPrivArray &plist,
    bool &has_other_priv)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool exists = false;
  has_other_priv = false;
  // 1. check p1
  OZ (ObOraPrivCheck::raw_sys_priv_exists(option, 
                                          p1, 
                                          priv_array, 
                                          exists));
  // 2. when has no p1, then fill "has_other_priv"  
  if (OB_SUCC(ret) && !exists) {
    tmp_ret = OB_ERR_NO_PRIVILEGE;
    for (int i = 0; i < plist.count() && !exists && OB_SUCC(ret); i++) {
      OZ (ObOraPrivCheck::raw_sys_priv_exists(option, 
                                              plist.at(i), 
                                              priv_array, 
                                              exists));
      if (OB_SUCC(ret) && exists) {
        has_other_priv = true;
      }                                         
    }
    OX (ret = tmp_ret);
  }

  return ret;
}

int ObOraSysChecker::check_p1_with_plist_info_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    ObRawPrivArray &plist,
    bool &has_other_priv)
{
  int ret = OB_SUCCESS;
  ObSysPriv *sys_priv = NULL;
  lib::Worker::CompatMode compat_mode;
  has_other_priv = false;
  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZ (guard.get_sys_priv_with_grantee_id(tenant_id, user_id, sys_priv));
    if (OB_SUCC(ret)) {
      if (sys_priv == NULL) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else {
        OZX1 (check_p1_with_plist_info_using_privs(
              sys_priv->get_priv_array(), p1, option, plist, has_other_priv),
                                           OB_ERR_NO_PRIVILEGE);
      }
    }
  }
  return ret;
}   


/* check user ?????????p1??????????????????????????????check plist option
  1. ???check ??????????????????
  2. ????????????????????????role??????????????? */
int ObOraSysChecker::check_p1_or_plist(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t option,
    const ObRawPrivArray &plist,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  // unused
  // ObSysPriv *sys_priv = NULL;
  lib::Worker::CompatMode compat_mode;

  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZX1 (check_p1_or_plist_in_single(guard, tenant_id, user_id, p1, option, plist),
                                     OB_ERR_NO_PRIVILEGE);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZ (check_p1_or_plist_in_roles(guard, tenant_id, user_id, p1, 
                                        option, plist, role_id_array));
    }
  }
  return ret;
}   

/* ???????????????????????????????????????????????? */
int ObOraSysChecker::check_plist_using_privs(
    const ObPackedPrivArray &priv_array,
    const uint64_t option,
    const ObRawPrivArray &plist)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  for (int i = 0; i < plist.count() && OB_SUCC(ret) && !exists; i++) {
    OZ (ObOraPrivCheck::raw_sys_priv_exists(option, 
                                            plist.at(i), 
                                            priv_array, 
                                            exists));
  }
  if (OB_SUCC(ret) && !exists) {
    ret = OB_ERR_NO_PRIVILEGE;
  }
  return ret;
}


int ObOraSysChecker::check_p1_or_plist_using_privs(
    const ObPackedPrivArray &priv_array,
    const ObRawPriv p1,
    const uint64_t option,
    const ObRawPrivArray &plist)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  
  // 1. check p1
  OZ (ObOraPrivCheck::raw_sys_priv_exists(option, 
                                                 p1, 
                                                 priv_array, 
                                                 exists));
  // 2. Or check plist 
  if (OB_SUCC(ret) && !exists) {  
    OZ (check_plist_using_privs(priv_array, option, plist));
  }

  return ret;
}

int ObOraSysChecker::check_p1_or_cond_p2_in_single(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t ur_id,
    const ObRawPriv p1,
    bool is_owner,
    const ObRawPriv p2)
{
  int ret = OB_SUCCESS;
  ObSysPriv *sys_priv = NULL;
  lib::Worker::CompatMode compat_mode;
  bool exists = false;
  
  OZ (guard.get_tenant_compat_mode(tenant_id, compat_mode));
  /* ??????oracle??????????????? */
  if (OB_SUCC(ret) && compat_mode == lib::Worker::CompatMode::ORACLE) {
    OZ (guard.get_sys_priv_with_grantee_id(tenant_id, ur_id, sys_priv));   
    if (OB_SUCC(ret)) {
      if (sys_priv == NULL) {
        ret = OB_ERR_NO_PRIVILEGE;
      } else {
        OZ (ObOraPrivCheck::p1_or_cond_p2_exists(p1, 
                                                 is_owner,
                                                 p2,
                                                 sys_priv->get_priv_array(), 
                                                 exists));
        if (OB_SUCC(ret) && !exists) {
          ret = OB_ERR_NO_PRIVILEGE;
        }
      }
    }
  }
  return ret;
}

int ObOraSysChecker::check_p1_or_cond_p2_in_roles(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t ur_id,
    const ObRawPriv p1,
    bool is_owner,
    const ObRawPriv p2,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObPackedPrivArray sys_packed_array;
  bool exists = false;
  
  OZ (get_user_sys_priv_in_roles(guard, tenant_id, ur_id, sys_packed_array, 
                                 true, role_id_array), 
      tenant_id, ur_id);
  OZ (ObOraPrivCheck::p1_or_cond_p2_exists(p1, 
                                            is_owner,
                                            p2,
                                            sys_packed_array,
                                            exists));
  if (OB_SUCC(ret) && !exists) {
    ret = OB_ERR_NO_PRIVILEGE;
  }
  return ret;
}

/*  ????????????user????????????????????????????????????????????????????????????owner??????????????????????????????
    1. check sys p1
    2. check owner and sys p2 
    ????????????create ??????, such as: create table, create view, create function, etc.
    */
int ObOraSysChecker::check_p1_or_owner_and_p2(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const ObRawPriv p1,
    const ObRawPriv p2,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  bool is_owner;
  lib::Worker::CompatMode compat_mode;
  
  if (OB_FAIL(guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
    LOG_WARN("get_tenant_compat_mode failed", K(ret));
  } else if (compat_mode == lib::Worker::CompatMode::ORACLE) {
    if (database_name.empty()) {
      is_owner = true;
    } else {
      const ObUserInfo *user_info = NULL;
      OZ (guard.get_user_info(tenant_id, user_id, user_info));
      if (OB_SUCC(ret) && NULL == user_info) {
        ret = OB_USER_NOT_EXIST;
        LOG_USER_ERROR(OB_USER_NOT_EXIST, database_name.length(), database_name.ptr());
      }
      OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
                                                   database_name));
    }
    OZX1 (check_p1_or_cond_p2_in_single(guard, tenant_id, user_id, p1, is_owner, p2),
        OB_ERR_NO_PRIVILEGE, tenant_id, user_id, p1, is_owner, p2);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZ (check_p1_or_cond_p2_in_roles(guard, tenant_id, user_id, p1, 
                                          is_owner, p2, role_id_array));
      
    }
  }
  return ret;
}


/*  ????????????user?????????owner????????????????????????????????????????????????????????????
    1. check owner 
    2. check sys p1
    3. check obj p2
    ????????????drop ??????, such as: drop table, drop view, drop function, etc.
    */
int ObOraSysChecker::check_owner_or_p1_or_objp2(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const uint64_t obj_type,
    const ObRawPriv p1,
    const ObRawObjPriv obj_p2,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  bool is_owner = false;
  uint64_t obj_owner_id = OB_INVALID_ID;
  lib::Worker::CompatMode compat_mode;
  
  if (OB_FAIL(guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
    LOG_WARN("get_tenant_compat_mode failed", K(ret));
  } else if (compat_mode == lib::Worker::CompatMode::ORACLE) {
    if (database_name.empty()) {
      is_owner = true;
      obj_owner_id = user_id;
    } else {
      const ObUserInfo *user_info = NULL;
      OZ (guard.get_user_info(tenant_id, user_id, user_info));
      if (OB_SUCC(ret) && NULL == user_info) {
        ret = OB_USER_NOT_EXIST;
        LOG_USER_ERROR(OB_USER_NOT_EXIST, database_name.length(), database_name.ptr());
      }
      OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
                                                   database_name));
      OZ (guard.get_user_id(tenant_id, database_name, 
                            ObString(OB_DEFAULT_HOST_NAME), obj_owner_id));
      if (OB_SUCC(ret) && obj_owner_id == OB_INVALID_ID) {
        ret = OB_USER_NOT_EXIST;
        LOG_USER_ERROR(OB_USER_NOT_EXIST, database_name.length(), database_name.ptr());
      }
    }
    if (!is_owner) {
      OZX1 (check_p1(guard, tenant_id, user_id, p1, role_id_array), 
                    OB_ERR_NO_PRIVILEGE, tenant_id, user_id, p1);
      if (ret == OB_ERR_NO_PRIVILEGE) {
        ret = OB_SUCCESS;
        OZ (check_ora_obj_priv(guard, tenant_id, user_id, database_name, obj_id, 
                               OBJ_LEVEL_FOR_TAB_PRIV, obj_type, obj_p2, CHECK_FLAG_NORMAL, 
                               obj_owner_id, role_id_array));
        /*OZ (check_obj_p1(guard, tenant_id, user_id, obj_type,
            obj_id, OBJ_LEVEL_FOR_TAB_PRIV, obj_p2, NO_OPTION, role_id_array),
            tenant_id, user_id, obj_type, obj_id, OBJ_LEVEL_FOR_TAB_PRIV, obj_p2);*/
      }
    }
  }
  return ret;
}

/** ????????????????????????????????????
 * 1. ???????????????owner
 * 2. ??????user_id????????????????????????p1
 * 3. ???????????????????????????????????????
 *   1. ??????????????????ret = OB_ERR_NO_PRIVILEGE
 *   2. ?????????????????????ret = OB_TABLE_NOT_EXIST
 * ??????oracle????????????drop table, drop view ???????????????
*/
int ObOraSysChecker::check_owner_or_p1_or_access(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const ObRawPriv p1,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  bool is_owner = false;
  lib::Worker::CompatMode compat_mode;
  if (OB_FAIL(guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
    LOG_WARN("get_tenant_compat_mode failed", K(ret));
  } else if (compat_mode == lib::Worker::CompatMode::ORACLE) {
    if (database_name.empty()) {
      is_owner = true;
    } else {
      const ObUserInfo *user_info = NULL;
      OZ (guard.get_user_info(tenant_id, user_id, user_info));
      if (OB_SUCC(ret)) {
        if (OB_ISNULL(user_info)) {
          ret = OB_USER_NOT_EXIST;
          LOG_USER_ERROR(OB_USER_NOT_EXIST, database_name.length(), database_name.ptr());
        }
      }
      // 1. Check if is owner
      OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
                                                   database_name));
    }
    if (OB_SUCC(ret)) {
      if (!is_owner) {
        // 2. Check sys priv
        OZX1 (check_p1(guard, tenant_id, user_id, p1, role_id_array),
                  OB_ERR_NO_PRIVILEGE, tenant_id, user_id, p1);
        // 3. Check related priv
        if (OB_ERR_NO_PRIVILEGE == ret) {
          ret = OB_SUCCESS;
          bool accessible = false;
          OZ (check_access_to_obj(guard, tenant_id, user_id,
                  p1, obj_type, obj_id, role_id_array, accessible));
          if (OB_SUCC(ret)) {
            if (accessible) {
              ret = OB_ERR_NO_PRIVILEGE;
            } else {
              ret = OB_TABLE_NOT_EXIST;
            }
          }
        }
      }
    }
  }
  return ret;
}

/** ??????user????????????access???obj_id??????????????????????????????????????????
 * 1. ??????user_id???????????????p1?????????????????????
 * 2. ??????user_id???????????????p1???obj_type?????????????????????
 * ?????? drop table, drop view ?????????????????????
*/
int ObOraSysChecker::check_access_to_obj(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObRawPriv p1,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const ObIArray<uint64_t> &role_id_array,
    bool &accessible)
{
  int ret = OB_SUCCESS;
  accessible = false;
  ObRawPrivArray sys_priv_array;
  ObRawObjPrivArray obj_priv_array;
  // 1. ?????????p1?????????????????????
  OZ (build_related_sys_priv_array(p1, sys_priv_array), p1);
  // 2. ??????user_id????????????sys_priv_array????????????????????????
  if (OB_SUCC(ret)) {
    if (sys_priv_array.count() > 0) {
      OZ (check_p1_or_plist(guard, tenant_id, user_id, 
            p1, NO_OPTION, sys_priv_array, role_id_array));
    } else {
      OZ (check_p1(guard, tenant_id, user_id, p1, role_id_array));
    }
    if (OB_SUCC(ret)) {
      accessible = true;
    } else {
      ret = OB_SUCCESS;
    }
  }
  if (OB_SUCC(ret) && !accessible) {
    // 3. ?????????p1???obj_type???????????????????????????
    OZ (build_related_obj_priv_array(p1, obj_type, obj_priv_array), p1, obj_type);
    // 4. ??????user_id??????obj_id????????????obj_priv_array??????????????????
    if (OB_SUCC(ret) && obj_priv_array.count() > 0) {
      OZ (check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id,
              OBJ_LEVEL_FOR_TAB_PRIV, obj_priv_array, role_id_array));
      if (OB_SUCC(ret)) {
        accessible = true;
      } else {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

// ???????????????????????????obj
int ObOraSysChecker::check_access_to_obj(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_type,
    const uint64_t obj_id,
    const ObIArray<uint64_t> &role_id_array,
    bool &accessible)
{
  int ret = OB_SUCCESS;
  accessible = false;
  ObRawPrivArray sys_priv_array;
  ObRawObjPrivArray obj_priv_array;
  // 1. ???????????????sys????????????
  OZ (build_related_sys_priv_array(obj_type, sys_priv_array));
  // 2. ??????user_id???????????? sys_priv ??????????????????
  if (OB_SUCC(ret) && sys_priv_array.count() > 0) {
    OZ (check_plist_or(guard, tenant_id, user_id, sys_priv_array, role_id_array));
    if (OB_SUCC(ret)) {
      accessible = true;
    } else if (OB_ERR_NO_PRIVILEGE == ret || OB_ERR_EMPTY_QUERY == ret) {
      accessible = false;
      ret = OB_SUCCESS;
    } else {
      accessible = false;
    }
  } 
  // 3. ???????????????????????????
  OZ (build_related_obj_priv_array(obj_type, obj_priv_array));
  // 4. ??????user_id ???????????? obj_priv_array????????????
  if (OB_SUCC(ret) && obj_priv_array.count() > 0 && !accessible) {
    OZ (check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id,
                          OBJ_LEVEL_FOR_TAB_PRIV, obj_priv_array, role_id_array));
    if (OB_SUCC(ret)) {
      accessible = true;
    } else if (OB_ERR_NO_PRIVILEGE == ret || OB_ERR_EMPTY_QUERY == ret) {
      accessible = false;
      ret = OB_SUCCESS;
    } else {
      accessible = false;
    }
  }
  return ret;
}

/**
 * ??????p1, obj_type???????????????????????????????????????
*/
int ObOraSysChecker::build_related_obj_priv_array(
    const ObRawPriv p1,
    const uint64_t obj_type,
    ObRawObjPrivArray &obj_priv_array)
{
  int ret = OB_SUCCESS;
  obj_priv_array.reset();
  switch (obj_type) {
    case static_cast<uint64_t>(share::schema::ObObjectType::TABLE):
      if (PRIV_ID_DROP_ANY_TABLE == p1 
         || PRIV_ID_SELECT_ANY_TABLE == p1
         || PRIV_ID_SELECT_ANY_DICTIONARY == p1) {
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_ALTER));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DEBUG));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DELETE));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INDEX));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INSERT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_READ));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_SELECT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
      } else if (PRIV_ID_CREATE_ANY_VIEW == p1
                || PRIV_ID_SELECT_ANY_TABLE == p1
                || PRIV_ID_SELECT_ANY_DICTIONARY == p1) {
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_ALTER));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DEBUG));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DELETE));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INSERT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_READ));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_SELECT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
      }
      break;
    case static_cast<uint64_t>(share::schema::ObObjectType::VIEW):
      if (PRIV_ID_DROP_ANY_VIEW == p1 
         || PRIV_ID_SELECT_ANY_TABLE == p1
         || PRIV_ID_SELECT_ANY_DICTIONARY == p1) {
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_ALTER));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DEBUG));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DELETE));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INDEX));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INSERT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_READ));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_SELECT));
        OZ (obj_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
      }
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexcepted obj_type", K(ret), K(obj_type));
      break;
  }
  return ret;
}

/**??????p1???????????????????????????????????????
 * 
*/
int ObOraSysChecker::build_related_sys_priv_array(
    const ObRawPriv p1,
    ObRawPrivArray &sys_priv_array)
{
  int ret = OB_SUCCESS;
  sys_priv_array.reset();
  switch (p1) {
    case static_cast<uint64_t>(PRIV_ID_DROP_ANY_TABLE):
    case static_cast<uint64_t>(PRIV_ID_SELECT_ANY_TABLE):
      OZ (sys_priv_array.push_back(PRIV_ID_CREATE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_ALTER_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_LOCK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_COMMENT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_SELECT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_INSERT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_UPDATE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_DELETE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
      break;
    case static_cast<uint64_t>(PRIV_ID_DROP_ANY_VIEW):
      OZ (sys_priv_array.push_back(PRIV_ID_ALTER_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_LOCK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_COMMENT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_SELECT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_INSERT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_UPDATE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_DELETE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_CREATE_ANY_VIEW));
      break;
    case static_cast<uint64_t>(PRIV_ID_CREATE_ANY_VIEW):
      OZ (sys_priv_array.push_back(PRIV_ID_ALTER_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_LOCK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_COMMENT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_SELECT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_INSERT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_UPDATE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_DELETE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_DROP_ANY_VIEW));
      break;
    case static_cast<uint64_t>(PRIV_ID_SELECT_ANY_DICTIONARY):
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexcepted sys_priv type", K(ret), K(p1));
      break;
  }
  return ret;
}

// ?????????????????????????????????????????????????????????
int ObOraSysChecker::build_related_sys_priv_array(
    const uint64_t obj_type,
    ObRawPrivArray &sys_priv_array)
{
  int ret = OB_SUCCESS;
  sys_priv_array.reset();
  switch (obj_type) {
    case static_cast<uint64_t>(ObObjectType::TABLE): {
      OZ (sys_priv_array.push_back(PRIV_ID_ALTER_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_LOCK_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_COMMENT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_SELECT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_INSERT_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_UPDATE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_DELETE_ANY_TABLE));
      OZ (sys_priv_array.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexcepted sys_priv type", K(ret), K(obj_type));
      break;
    }
  }
  return ret;
}

int ObOraSysChecker::build_related_obj_priv_array(
    const uint64_t obj_type,
    ObRawObjPrivArray &obj_priv_array)
{
  int ret = OB_SUCCESS;
  obj_priv_array.reset();
  switch (obj_type) {
    case static_cast<uint64_t>(ObObjectType::TABLE): {
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_ALTER));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DEBUG));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_DELETE));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INDEX));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_INSERT));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_READ));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_REFERENCES));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_SELECT));
      OZ (obj_priv_array.push_back(OBJ_PRIV_ID_UPDATE));
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid obj_type", K(ret), K(obj_type));
      break;
    }
  }
  return ret;
}

/*  ????????????user????????????????????????????????????????????????????????????owner??????????????????????????????
    1. check owner 
    2. check sys p1
    ????????????drop ??????, such as: drop table, drop view, drop function, etc.
    */
int ObOraSysChecker::check_owner_or_p1(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const ObRawPriv p1,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  // unused
  // ObSysPriv *sys_priv = NULL;
  bool is_owner = false;
  lib::Worker::CompatMode compat_mode;
  // unused
  // bool exists = false;
  
  if (OB_FAIL(guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
    LOG_WARN("get_tenant_compat_mode failed", K(ret));
  } else if (compat_mode == lib::Worker::CompatMode::ORACLE) {
    if (database_name.empty()) {
      is_owner = true;
    } else {
      const ObUserInfo *user_info = NULL;
      OZ (guard.get_user_info(tenant_id, user_id, user_info));
      if (OB_SUCC(ret) && NULL == user_info) {
        ret = OB_USER_NOT_EXIST;
        LOG_USER_ERROR(OB_USER_NOT_EXIST, database_name.length(), database_name.ptr());
      }
      OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
                                                   database_name));
    }
    if (!is_owner) {
      OZ (check_p1(guard, tenant_id, user_id, p1, role_id_array), tenant_id, user_id, p1);
    }
  }
  return ret;
}

/* ???????????????
1. check is owner
2. if owner return success
3. else has sys priv p1, return success 
4. else has obj priv p2, return success 
5. else return no_privilege */
// int ObOraSysChecker::check_ora_alter_ddl_priv_inner(
//     ObSchemaGetterGuard &guard,
//     const uint64_t tenant_id,
//     const uint64_t user_id,
//     const ObString &database_name,
//     const ObDbObjType db_obj_type,
//     const ObString &objname,
//     const ObRawPriv p1,
//     const ObObjRawPriv p2)
// {
//   int ret = OB_SUCCESS;
//   ObSysPriv *sys_priv = NULL;
//   bool is_owner;
//   lib::Worker::CompatMode compat_mode;
//   bool exists = false;
  
//   if (OB_FAIL(guard.get_tenant_compat_mode(tenant_id, compat_mode))) {
//     LOG_WARN("get_tenant_compat_mode failed", K(ret));
//   } else if (compat_mode == lib::Worker::CompatMode::ORACLE) {
//     if (database_name.empty()) {
//       is_owner = true;
//     } else {
//       const ObUserInfo *user_info = NULL;
//       OZ (guard.get_user_info(user_id, user_info));
//       OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
//                                                           database_name));
//     }
//     OZ (check_cond_p1_or_op2(guard, tenant_id, user_id, db_obj_type, objname, is_owner, p1, p2),
//         tenant_id, user_id, is_owner, p1, p2);
//     if (ret == OB_ERR_NO_PRIVILEGE) {
//       ret = OB_SUCCESS;
//       ObPackedPrivArray sys_packed_array;
//       OZ (get_user_sys_priv_in_roles(guard, tenant_id, user_id, sys_packed_array), 
//           tenant_id, user_id);
//       OZ (ObOraPrivCheck::p1_or_cond_p2_exists(p1, 
//                                                       is_owner,
//                                                       p2,
//                                                       sys_packed_array,
//                                                       exists));
//       if (OB_SUCC(ret) && !exists) {
//         ret = OB_ERR_NO_PRIVILEGE;
//       }
//     }
//   }
//   return ret;
// }

/* check owner, ??????sys priv??????????????????create table???????????????  */
#define DEFINE_CREATE_CHECK_CMD(p1, p2)           \
      OZ (check_p1_or_owner_and_p2(guard,         \
                                   tenant_id,     \
                                   user_id,       \
                                   database_name, \
                                   p1,            \
                                   p2,            \
                                   role_id_array),           \
          tenant_id, user_id, database_name, p1, p2);

/* check owner, ??????sys priv ????????????obj priv??????????????????alter table ??????????????? */
#define DEFINE_ALTER_CHECK_CMD(p1, obj_p2)           \
      OZ (check_owner_or_p1_or_objp2(guard,         \
                                   tenant_id,     \
                                   user_id,       \
                                   database_name, \
                                   obj_id,        \
                                   obj_type,      \
                                   p1,            \
                                   obj_p2,        \
                                   role_id_array),           \
          tenant_id, user_id, database_name, p1, obj_p2);

/* check owner, ??????sys priv??? ???????????????drop table??????????????? */
#define DEFINE_DROP_CHECK_CMD(p1)           \
      OZ (check_owner_or_p1(guard,         \
                                     tenant_id,     \
                                     user_id,       \
                                     database_name, \
                                     p1,            \
                                     role_id_array),           \
          tenant_id, user_id, database_name, p1);

/* check owner?????????sys priv???????????????sys priv?????????????????????????????????
   ???????????????????????????????????????????????????????????????????????????*/
#define DEFINE_DROP_VIEW_CHECK_CMD(p1)                      \
         OZ (check_owner_or_p1_or_access(guard,             \
                                         tenant_id,         \
                                         user_id,           \
                                         database_name,     \
                                         p1,                \
                                         obj_type, \
                                         obj_id,   \
                                         role_id_array),    \
          tenant_id, user_id, database_name, p1);

/** check owner, ??????sys priv???????????????sys priv????????????user_id??????
 * ???????????????????????????????????????????????????????????????????????????????????????
*/
#define DEFINE_DROP_TABLE_CHECK_CMD(p1)                      \
         OZ (check_owner_or_p1_or_access(guard,             \
                                         tenant_id,         \
                                         user_id,           \
                                         database_name,     \
                                         p1,                \
                                         obj_type, \
                                         obj_id,   \
                                         role_id_array),    \
          tenant_id, user_id, database_name, p1);

/* check ??????sys priv??????????????????create pub synonym */
#define DEFINE_PUB_CHECK_CMD(p1)           \
      OZ (check_p1(guard,         \
                   tenant_id,     \
                   user_id,       \
                   p1,            \
                   role_id_array),           \
          tenant_id, user_id, p1);

/* check ??????sys priv??????????????????purge table/index/recyclebin */
#define DEFINE_PURGE_CHECK_CMD(p1, p2)           \
      OZX1 (check_p1(guard,         \
                     tenant_id,     \
                     user_id,       \
                     p1,            \
                     role_id_array),           \
            OB_ERR_NO_PRIVILEGE, tenant_id, user_id, p1); \
      if (ret == OB_ERR_NO_PRIVILEGE) {                   \
        ret = OB_SUCCESS;                                 \
        OZ (check_p1(guard,                               \
                     tenant_id,                           \
                     user_id,                             \
                     p2,                                  \
                     role_id_array),                      \
          tenant_id, user_id, p2);                        \
      }

/* sp: sys priv
   op: obj priv */


/* ?????????sys priv???grant?????????grantor????????????owner????????????????????????owner?????????
        ???????????????revoke????????????????????????????????????????????????????????????
   ????????????sys priv???grant?????????grantor?????????grant?????????????????? */
int ObOraSysChecker::decide_grantor_id(
    ObSchemaGetterGuard &guard,
    bool has_sys_priv,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    uint64_t &grantor_id_out)
{
  int ret = OB_SUCCESS;  
  if (has_sys_priv) {
    /* set ownerid to grantor_id */
    if (database_name.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("database name is null.");
    } else {
      const ObUserInfo *user_info = NULL;          
      OZ (guard.get_user_info(tenant_id, database_name, 
                              ObString(OB_DEFAULT_HOST_NAME), user_info));
      if (OB_SUCC(ret)) {
        if (user_info != NULL) {
          grantor_id_out = user_info->get_user_id();
        } else {
          ret = OB_ERR_BAD_DATABASE;
          LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
          LOG_WARN("invalid database name. ", K(database_name));
        }
      }
    }
  } else {
    grantor_id_out = user_id;
  }
  return ret;
}

int ObOraSysChecker::check_single_option_obj_priv_inner(
    ObPackedObjPriv &ur_privs,
    uint64_t option,
    ObRawObjPriv raw_priv)
{
  int ret = OB_SUCCESS;
  bool exists = false;

  /* ???????????????????????????????????????empty_query */
  if (ur_privs == OBJ_PRIV_ID_NONE) {
    ret = OB_ERR_EMPTY_QUERY;
  } else {
    OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, option, ur_privs, exists));
    if (OB_SUCC(ret) && !exists) {
      ret = OB_ERR_NO_PRIVILEGE;
    }
  }
  return ret;
}    

/* ??????????????????????????????????????????????????? 
 in??? ???????????????????????????????????????
 in??? ???check????????? */
int ObOraSysChecker::check_single_option_obj_priv_inner(
    ObPackedObjPriv &ur_privs,
    ObRawObjPriv raw_priv)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  if (ur_privs == OBJ_PRIV_ID_NONE) {
    ret = OB_ERR_EMPTY_QUERY;
  } else {
    OZ (ObOraPrivCheck::raw_obj_priv_exists(raw_priv, GRANT_OPTION, ur_privs, exists));
    if (OB_SUCC(ret) && !exists) {
      ret = OB_ERR_NO_PRIVILEGE;
    }
  }
  return ret;
}

/* ????????????????????????????????????????????? 
 in??? ???????????????????????????????????????
 in??? ???check???????????? */
int ObOraSysChecker::check_multi_option_obj_privs_inner(
    ObPackedObjPriv &ur_privs,
    const ObRawObjPrivArray &table_privs)
{
  int ret = OB_SUCCESS;
  int i = 0;
  for (i = 0; i < table_privs.count() && OB_SUCC(ret); i++) {
    OZ (check_single_option_obj_priv_inner(ur_privs, table_privs.at(i)));
  }
  return ret;
}

int ObOraSysChecker::check_objplist_in_single(
    ObSchemaGetterGuard &guard,
    ObObjPrivSortKey &obj_key,
    const ObRawObjPrivArray &table_privs,
    const ObIArray<uint64_t> &ins_col_id,
    const ObIArray<uint64_t> &upd_col_id,
    const ObIArray<uint64_t> &ref_col_id)
{
  int ret = OB_SUCCESS;
  uint64_t tenant_id = obj_key.tenant_id_;
  ObPackedObjPriv obj_privs = 0;
  ObRawObjPriv priv;
  // 1. check table privs 
  OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, obj_privs), obj_key);
  OZX1 (check_multi_option_obj_privs_inner(obj_privs, table_privs), OB_ERR_NO_PRIVILEGE);
  //2. check ins col privs
  priv = OBJ_PRIV_ID_INSERT;
  for (int i = 0; i < ins_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = ins_col_id.at(i));
    OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, obj_privs), obj_key);
    OZX1 (check_single_option_obj_priv_inner(obj_privs, priv), OB_ERR_NO_PRIVILEGE);
  }
  //3. check upd col privs
  priv = OBJ_PRIV_ID_UPDATE;
  for (int i = 0; i < upd_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = upd_col_id.at(i));
    OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, obj_privs), obj_key);
    OZX1 (check_single_option_obj_priv_inner(obj_privs, priv), OB_ERR_NO_PRIVILEGE);
  }
  //4. check ref col privs
  priv = OBJ_PRIV_ID_REFERENCES;
  for (int i = 0; i < ref_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = ref_col_id.at(i));
    OZ (guard.get_obj_privs_in_ur_and_obj(tenant_id, obj_key, obj_privs), obj_key);
    OZX1 (check_single_option_obj_priv_inner(obj_privs, priv), OB_ERR_NO_PRIVILEGE);
  }
  return ret;
}

int ObOraSysChecker::check_objplist_in_roles(
    ObSchemaGetterGuard &guard,
    ObObjPrivSortKey &obj_key,
    const ObRawObjPrivArray &table_privs,
    const ObIArray<uint64_t> &ins_col_id,
    const ObIArray<uint64_t> &upd_col_id,
    const ObIArray<uint64_t> &ref_col_id,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObPackedObjPriv obj_privs = 0;

  // 1. check table privs 
  OZ (get_roles_obj_privs(guard, obj_key, obj_privs, true, role_id_array));
  OZ (check_multi_option_obj_privs_inner(obj_privs, table_privs));
  //2. check ins col privs
  for (int i = 0; i < ins_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = ins_col_id.at(i));
    OZ (get_roles_obj_privs(guard, obj_key, obj_privs, true, role_id_array));
    OZ (check_single_option_obj_priv_inner(obj_privs, OBJ_PRIV_ID_INSERT));
  }
  //3. check upd col privs
  for (int i = 0; i < upd_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = upd_col_id.at(i));
    OZ (get_roles_obj_privs(guard, obj_key, obj_privs, true, role_id_array));
    OZ (check_single_option_obj_priv_inner(obj_privs, OBJ_PRIV_ID_UPDATE));
  }
  //4. check ref col privs
  for (int i = 0; i < ref_col_id.count() && OB_SUCC(ret); i++) {
    OX (obj_key.col_id_ = ref_col_id.at(i));
    OZ (get_roles_obj_privs(guard, obj_key, obj_privs, true, role_id_array));
    OZ (check_single_option_obj_priv_inner(obj_privs, OBJ_PRIV_ID_REFERENCES));
  }
  return ret;
}

/* check grant objauth privs */
int ObOraSysChecker::check_objplist(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_id,
    const uint64_t obj_type,
    const ObRawObjPrivArray &table_privs,
    const ObIArray<uint64_t> &ins_col_id,
    const ObIArray<uint64_t> &upd_col_id,
    const ObIArray<uint64_t> &ref_col_id,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObObjPrivSortKey obj_key(tenant_id, obj_id, obj_type, OBJ_LEVEL_FOR_TAB_PRIV, 
                           user_id, user_id);
  OZX2 (check_objplist_in_single(guard, obj_key, table_privs, 
                                ins_col_id, upd_col_id, ref_col_id),
        OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY);
  if (ret == OB_ERR_NO_PRIVILEGE || ret == OB_ERR_EMPTY_QUERY) {
    ret = OB_SUCCESS;
    OZ (check_objplist_in_roles(guard, obj_key, table_privs,
        ins_col_id, upd_col_id, ref_col_id, role_id_array));
  }
  return ret;
}

/* ???create view?????????check???
   check ??????????????????????????????select, insert, update, delete?????? */
int ObOraSysChecker::check_ora_obj_priv_for_create_view(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const uint64_t col_id,
    const uint64_t obj_type,
    const uint64_t obj_owner_id)
{
  int ret = OB_SUCCESS;
  UNUSED(database_name);
  bool is_owner = is_owner_user(user_id, obj_owner_id);

  if (OB_SUCC(ret)) {
    if (obj_type == static_cast<uint64_t>(ObObjectType::TABLE)) {
      /* 1. check owner*/
      if (!is_owner) {
      /* 2. check sys priv */
        ObRawPrivArray priv_list;
        if (!is_ora_sys_view_table(obj_id)) {
          OZ (priv_list.push_back(PRIV_ID_SELECT_ANY_TABLE));
          OZ (priv_list.push_back(PRIV_ID_INSERT_ANY_TABLE));
          OZ (priv_list.push_back(PRIV_ID_UPDATE_ANY_TABLE));
          OZ (priv_list.push_back(PRIV_ID_DELETE_ANY_TABLE));
        } else {
          OZ (priv_list.push_back(PRIV_ID_SELECT_ANY_DICTIONARY));
        }
        
        OZX1 (check_plist_or_in_single(guard, tenant_id, user_id, priv_list), OB_ERR_NO_PRIVILEGE);
        if (ret == OB_ERR_NO_PRIVILEGE) {
          /* 3. check obj priv */
          ret = OB_SUCCESS;
          ObRawObjPrivArray obj_p_list;
          OZ (obj_p_list.push_back(OBJ_PRIV_ID_SELECT));
          OZ (obj_p_list.push_back(OBJ_PRIV_ID_INSERT));
          OZ (obj_p_list.push_back(OBJ_PRIV_ID_UPDATE));
          OZ (obj_p_list.push_back(OBJ_PRIV_ID_DELETE));
        
          OZ (check_obj_plist_or_in_single(guard, tenant_id, user_id, obj_type, 
                                           obj_id, col_id, obj_p_list),
                tenant_id, user_id, obj_type, obj_id, col_id, obj_p_list);  
        }
        /* ??????????????? to table or view not exists */
        if (ret == OB_ERR_EMPTY_QUERY) {
          ret = OB_TABLE_NOT_EXIST;
        }
      }
    } else if (obj_type == static_cast<uint64_t>(ObObjectType::SEQUENCE)) {
      if (!is_owner) {
        ObRawPrivArray priv_list;
        OZ (priv_list.push_back(PRIV_ID_SELECT_ANY_SEQ));
        OZX1 (check_plist_or_in_single(guard, tenant_id, user_id, priv_list), OB_ERR_NO_PRIVILEGE);
        if (ret == OB_ERR_NO_PRIVILEGE) {
          /* 3. check obj priv */
          ret = OB_SUCCESS;
          OZ (check_obj_p1_in_single(guard, tenant_id, user_id, obj_type,
                                     obj_id, col_id, OBJ_PRIV_ID_SELECT, NO_OPTION),
              tenant_id, user_id, obj_type, obj_id, col_id, OBJ_PRIV_ID_SELECT, NO_OPTION);
        }
      }
    } /* xinqi to do: else if */
  }

  return ret;
}

/* ??????sys_priv??????????????????other sys privs .
   ????????????????????????????????????sys priv???
   ?????? ?????????select any table??? ??????: create/alter/drop/update/delete any table */
int ObOraSysChecker::build_related_plist(
    uint64_t obj_type,
    ObRawPriv& sys_priv,
    share::ObRawPrivArray& plist)
{
  int ret = OB_SUCCESS;
  if (obj_type == static_cast<uint64_t>(ObObjectType::VIEW)) {
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_VIEW));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TABLE));
  }
  switch (sys_priv)
  {
  case PRIV_ID_SELECT_ANY_TABLE:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_LOCK_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_COMMENT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_INSERT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_UPDATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DELETE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
    break;
  
  case PRIV_ID_INSERT_ANY_TABLE:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_LOCK_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_COMMENT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_SELECT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_UPDATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DELETE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
    break;

  case PRIV_ID_UPDATE_ANY_TABLE:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_LOCK_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_COMMENT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_SELECT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_INSERT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DELETE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
    break;

  case PRIV_ID_DELETE_ANY_TABLE:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_LOCK_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_COMMENT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_SELECT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_INSERT_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_UPDATE_ANY_TABLE));
    OZ (plist.push_back(PRIV_ID_FLASHBACK_ANY_TABLE));
    break;
  
  case PRIV_ID_EXEC_ANY_PROC:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_PROC));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_PROC));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_PROC));
    break;

  case PRIV_ID_EXEC_ANY_TYPE:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_TYPE));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_TYPE));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TYPE));
    OZ (plist.push_back(PRIV_ID_UNDER_ANY_TYPE));
    break;

  case PRIV_ID_SELECT_ANY_SEQ:
    OZ (plist.push_back(PRIV_ID_CREATE_ANY_SEQ));
    OZ (plist.push_back(PRIV_ID_ALTER_ANY_SEQ));
    OZ (plist.push_back(PRIV_ID_DROP_ANY_TYPE));
    break;

  default:
    break;
  }
  return ret;
}

int ObOraSysChecker::map_obj_priv_to_sys_priv(
    const ObRawObjPriv raw_obj_priv,
    const uint64_t obj_id,
    const uint64_t obj_type,
    ObRawPriv& sys_priv)
{
  int ret = OB_SUCCESS;
 
  if (!is_ora_sys_view_table(obj_id)) {
    switch (raw_obj_priv) {
      case OBJ_PRIV_ID_SELECT:
        if (obj_type == static_cast<uint64_t>(ObObjectType::SEQUENCE)) {
          sys_priv = PRIV_ID_SELECT_ANY_SEQ;
        } else if (obj_type == static_cast<uint64_t>(ObObjectType::TRIGGER)) {
          sys_priv = PRIV_ID_CREATE_ANY_TRIG;
        } else {
          sys_priv = PRIV_ID_SELECT_ANY_TABLE;
        }
        break;
      case OBJ_PRIV_ID_INSERT:
        sys_priv = PRIV_ID_INSERT_ANY_TABLE;
        break;
      case OBJ_PRIV_ID_UPDATE:
        sys_priv = PRIV_ID_UPDATE_ANY_TABLE;
        break;
      case OBJ_PRIV_ID_DELETE:
        sys_priv = PRIV_ID_DELETE_ANY_TABLE;
        break;
      case OBJ_PRIV_ID_EXECUTE:
        if (obj_type == static_cast<uint64_t>(ObObjectType::TYPE)) {
          sys_priv = PRIV_ID_EXEC_ANY_TYPE;
        }
        else { 
          sys_priv = PRIV_ID_EXEC_ANY_PROC;
        }
        break;
      case OBJ_PRIV_ID_DEBUG:
        sys_priv = PRIV_ID_DEBUG_ANY_PROC;
        break;
      case OBJ_PRIV_ID_INDEX:
      case OBJ_PRIV_ID_REFERENCES:
      case OBJ_PRIV_ID_ALTER:
        /* no sys priv */
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("raw obj priv is invalid", K(raw_obj_priv), K(ret));
        break;
    }
  } else {
    sys_priv = PRIV_ID_SELECT_ANY_DICTIONARY;
  }
  return ret;
}

int ObOraSysChecker::map_obj_priv_array_to_sys_priv_array(
    const ObIArray<ObRawObjPriv> &raw_obj_priv_array,
    const uint64_t obj_id,
    const uint64_t obj_type,
    ObIArray<ObRawPriv> &sys_priv_array)
{
  int ret = OB_SUCCESS;
  
  for (int i = 0; OB_SUCC(ret) && i < raw_obj_priv_array.count(); i++) {
    ObRawPriv sys_priv = PRIV_ID_NONE;
    OZ (map_obj_priv_to_sys_priv(raw_obj_priv_array.at(i), obj_id, obj_type, sys_priv));
    if (OB_SUCC(ret) && sys_priv != PRIV_ID_NONE) {
      OZ (sys_priv_array.push_back(sys_priv));
    }
  }
  return ret;
}

/* ???????????????check???????????????????????????????????????????????????????????? 
  1. ???check is owner
  2. ???check syspriv
  3. ??????check obj priv
  4. ????????????????????? */
int ObOraSysChecker::check_ora_obj_privs_or(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_db_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const uint64_t col_id,
    const uint64_t org_obj_type,
    const ObRawObjPrivArray &raw_obj_priv_array,
    uint64_t check_flag,
    const uint64_t obj_owner_id,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  uint64_t obj_type;
  uint64_t option = 0;
  UNUSED(database_name);
  obj_type = org_obj_type;
  if (org_obj_type == static_cast<uint64_t>(ObObjectType::VIEW) ||
      org_obj_type == static_cast<uint64_t>(ObObjectType::TRIGGER)) {
    obj_type = static_cast<uint64_t>(ObObjectType::TABLE);
  }
  if (!is_super_user(user_id)) {
    CK (check_flag != CHECK_FLAG_DIRECT && check_flag != CHECK_FLAG_WITH_GRANT_OPTION);
    bool is_owner = is_owner_user(user_db_id, obj_owner_id);
      
    if (OB_SUCC(ret)) {
      if (obj_type == static_cast<uint64_t>(ObObjectType::TABLE)
          || obj_type == static_cast<uint64_t>(ObObjectType::INDEX) 
          || obj_type == static_cast<uint64_t>(ObObjectType::FUNCTION) 
          || obj_type == static_cast<uint64_t>(ObObjectType::PROCEDURE)
          || obj_type == static_cast<uint64_t>(ObObjectType::PACKAGE)
          || obj_type == static_cast<uint64_t>(ObObjectType::TYPE)
          || obj_type == static_cast<uint64_t>(ObObjectType::SEQUENCE)
          || obj_type == static_cast<uint64_t>(ObObjectType::TRIGGER)) {
        /* 1. check owner*/
        if (!is_owner) {
        /* 2. check sys priv */
          ObRawPrivArray sys_priv_array;
          share::ObRawPrivArray plist;
          /* ???????????????obj type????????????,trigger ??????
            ????????????????????????????????????????????????
            ??????trigger??????????????????????????????PRIV_ID_CREATE_ANY_TRIG
                ?????????create any trigger??????????????????????????????trigger
          */
          OZ (map_obj_priv_array_to_sys_priv_array(raw_obj_priv_array, 
                                                   obj_id, 
                                                   org_obj_type, 
                                                   sys_priv_array), 
              raw_obj_priv_array, sys_priv_array);
          
          OX (option = NO_OPTION);
          OZX1 (check_plist_or(guard, tenant_id, user_id, sys_priv_array, role_id_array),
                               OB_ERR_NO_PRIVILEGE, tenant_id, user_id, sys_priv_array);
          if (ret == OB_ERR_NO_PRIVILEGE) {
            /* 3. check obj priv */
            ret = OB_SUCCESS;
            OZ (check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id, 
                col_id, raw_obj_priv_array, role_id_array),
                tenant_id, user_id, obj_type, obj_id, col_id, raw_obj_priv_array);
          } 

          /* ??????????????? */
          if (!OB_SUCC(ret)) {
            /* a. normal: to table or view not exists */
            if (ret == OB_ERR_EMPTY_QUERY) {
              ret = OB_ERR_NO_PRIVILEGE;
            } 
          }
        } 
      } else if (obj_type == static_cast<uint64_t>(ObObjectType::DIRECTORY)) {
        /* directory?????????owner???sys */
        OZ (check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id,
                                  col_id, raw_obj_priv_array, role_id_array),
            tenant_id, user_id, obj_type, obj_id, col_id, raw_obj_priv_array, NO_OPTION);
      
        /* ??????????????? */
        if (!OB_SUCC(ret)) {
          ret = OB_ERR_DIRECTORY_ACCESS_DENIED;
        }
      } else {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("Wrong obj type", K(obj_type));
      }
    }
    /* ??????oracle????????? */
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_ERR_NO_SYS_PRIVILEGE;
    }
  }
  return ret;
}

/* ???????????????check???????????????????????????????????????????????? 
  1. ???check is owner
  2. ???check syspriv
  3. ??????check obj priv
  
  4. ????????????????????? */
int ObOraSysChecker::check_ora_obj_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const uint64_t col_id,
    const uint64_t org_obj_type,
    const ObRawObjPriv raw_obj_priv,
    uint64_t check_flag,
    const uint64_t obj_owner_id,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  uint64_t obj_type;
  uint64_t option = 0;
  obj_type = org_obj_type;
  if (org_obj_type == static_cast<uint64_t>(ObObjectType::VIEW)) {
    obj_type = static_cast<uint64_t>(ObObjectType::TABLE);
  }
  if (!is_super_user(user_id)) {
    if (check_flag == CHECK_FLAG_DIRECT) {
      OZ (check_ora_obj_priv_for_create_view(guard,
                                            tenant_id,
                                            user_id,
                                            database_name,
                                            obj_id, 
                                            col_id,
                                            obj_type,
                                            obj_owner_id));
    } else {
      bool is_owner = is_owner_user(user_id, obj_owner_id);
      
      if (OB_SUCC(ret)) {
        if (obj_type == static_cast<uint64_t>(ObObjectType::TABLE)
            || obj_type == static_cast<uint64_t>(ObObjectType::INDEX) 
            || obj_type == static_cast<uint64_t>(ObObjectType::FUNCTION) 
            || obj_type == static_cast<uint64_t>(ObObjectType::PROCEDURE)
            || obj_type == static_cast<uint64_t>(ObObjectType::PACKAGE)
            || obj_type == static_cast<uint64_t>(ObObjectType::SYS_PACKAGE)
            || obj_type == static_cast<uint64_t>(ObObjectType::SYS_PACKAGE_ONLY_OBJ_PRIV)
            || obj_type == static_cast<uint64_t>(ObObjectType::TYPE)
            || obj_type == static_cast<uint64_t>(ObObjectType::SEQUENCE)) {
          /* 1. check owner*/
          if (!is_owner) {
            ObRawPriv sys_priv = PRIV_ID_NONE;
            share::ObRawPrivArray plist;
            bool has_other_priv = false;
            /* 2. check sys priv */
            if (obj_type != static_cast<uint64_t>(ObObjectType::SYS_PACKAGE_ONLY_OBJ_PRIV)) {
              
              /* ???????????????obj type??????????????????
                ???????????????dml?????????????????? 
              */
              OZ (map_obj_priv_to_sys_priv(raw_obj_priv, obj_id, org_obj_type, sys_priv), 
                  raw_obj_priv, sys_priv);
              OZ (build_related_plist(org_obj_type, sys_priv, plist));
              
              OX (option = (check_flag != CHECK_FLAG_WITH_GRANT_OPTION) ? NO_OPTION : GRANT_OPTION);
              if (!plist.empty()) {
                OZX1 (check_p1_with_plist_info(guard, tenant_id, user_id, sys_priv,
                                              option, plist, has_other_priv, role_id_array),
                                              OB_ERR_NO_PRIVILEGE, tenant_id, user_id, sys_priv);
              } else {
                has_other_priv = true;
                OZX1 (check_p1(guard, tenant_id, user_id, sys_priv, role_id_array),
                              OB_ERR_NO_PRIVILEGE, tenant_id, user_id, sys_priv);
              }
            } else {
              ret = OB_ERR_NO_PRIVILEGE;
              obj_type = static_cast<uint64_t>(ObObjectType::SYS_PACKAGE);
            }
            if (ret == OB_ERR_NO_PRIVILEGE) {
              /* 3.a check obj priv */
              ret = OB_SUCCESS;
              OZX1 (check_obj_p1(guard, tenant_id, user_id, obj_type, obj_id, 
                  col_id, raw_obj_priv, option, role_id_array), OB_ERR_EMPTY_QUERY,
                  tenant_id, user_id, obj_type, obj_id, col_id, raw_obj_priv);
            } else if (OB_SUCC(ret) && check_flag == CHECK_FLAG_WITH_GRANT_OPTION) {
              /* 3.b when check grant option, need check obj privs */
              OZ (check_obj_p1(guard, tenant_id, user_id, obj_type, obj_id, 
                  col_id, raw_obj_priv, option, role_id_array),
                  tenant_id, user_id, obj_type, obj_id, col_id, raw_obj_priv);
            }

            /* ??????????????? */
            if (!OB_SUCC(ret)) {
              if (check_flag != CHECK_FLAG_WITH_GRANT_OPTION) {
                /* a. normal: to table or view not exists */
                if (ret == OB_ERR_EMPTY_QUERY) {
                  if (!has_other_priv) {
                    if (obj_type == static_cast<uint64_t>(ObObjectType::FUNCTION)) {
                      ret = OB_WRONG_COLUMN_NAME;
                    } else {
                      ret = OB_TABLE_NOT_EXIST;
                    }
                  } else {
                    ret = OB_ERR_NO_PRIVILEGE;
                  }
                }
              } else if (ret == OB_ERR_EMPTY_QUERY || OB_ERR_NO_PRIVILEGE) {
                /* b. check grantable: with grant option 
                      ??????????????? ORA-01720: grant option does not exist for */
                const ObSimpleTableSchemaV2 *view_schema = NULL;
                if (OB_FAIL(guard.get_simple_table_schema(
                            tenant_id, obj_id, view_schema))) {
                  LOG_WARN("failed to get table schema", K(ret));
                } else if (OB_ISNULL(view_schema)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("null table schema", K(ret));
                } else {
                  ret = OB_ERR_NO_GRANT_OPTION;
                  LOG_USER_ERROR(OB_ERR_NO_GRANT_OPTION, 
                                database_name.length(), 
                                database_name.ptr(),
                                view_schema->get_table_name_str().length(), 
                                view_schema->get_table_name_str().ptr());
                }
              }
            }
          }
        } else if (obj_type == static_cast<uint64_t>(ObObjectType::DIRECTORY)) {
          /* directory?????????owner???sys */
          OZ (check_obj_p1(guard, tenant_id, user_id, obj_type,
                          obj_id, col_id, raw_obj_priv, NO_OPTION, role_id_array),
              tenant_id, user_id, obj_type, obj_id, col_id, raw_obj_priv, NO_OPTION);
        
          /* ??????????????? */
          if (!OB_SUCC(ret)) {
            ret = OB_ERR_DIRECTORY_ACCESS_DENIED;
          }
        } else {
          ret = OB_TABLE_NOT_EXIST;
          LOG_WARN("Wrong obj type", K(obj_type));
        }
      }
    }
    /* ??????oracle????????? */
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_ERR_NO_SYS_PRIVILEGE;
    }
  }
  return ret;
}

/* ???????????????????????????????????????????????? */
int ObOraSysChecker::check_ora_connect_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObIArray<uint64_t> &role_id_array)
{  
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    const ObUserInfo *user_info = NULL;
    ObRawPriv need_priv = PRIV_ID_CREATE_SESSION;
    OZ (guard.get_user_info(tenant_id, user_id, user_info), user_id);
    OZ (check_p1(guard, tenant_id, user_id, need_priv, role_id_array));
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_ERR_NO_LOGIN_PRIVILEGE;
      if (user_info != NULL) {
        LOG_USER_ERROR(OB_ERR_NO_LOGIN_PRIVILEGE, 
                        user_info->get_user_name_str().length(),
                        user_info->get_user_name_str().ptr());
      }
    }
  }
  return ret;
}    

/* ddl?????????????????????????????????????????????????????????
   1. ??????????????????
   2. ?????????????????? */
int ObOraSysChecker::check_ora_ddl_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    stmt::StmtType stmt_type,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    switch (stmt_type) {
      case stmt::T_CREATE_TABLE: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_TABLE, PRIV_ID_CREATE_TABLE);
        break;
      }
      /* observer to do : alter index 
      case stmt::T_ALTER_INDEX: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_ALTER_ANY_INDEX);
        break;
      }*/
      case stmt::T_DROP_INDEX: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_INDEX);
        break;
      }
      case stmt::T_CREATE_VIEW: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_VIEW, PRIV_ID_CREATE_VIEW);
        break;
      }
      case stmt::T_CREATE_ROUTINE: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_PROC, PRIV_ID_CREATE_PROC);
        break;  
      }
      case stmt::T_ALTER_ROUTINE: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_ALTER_ANY_PROC);
        break;
      }
      case stmt::T_DROP_ROUTINE: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_PROC);
        break;
      }
      case stmt::T_CREATE_SYNONYM: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_SYN, PRIV_ID_CREATE_SYN);
        break;
      }
      case stmt::T_DROP_SYNONYM: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_SYN);
        break;
      }
      case stmt::T_CREATE_PUB_SYNONYM: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_PUB_SYN);
        break;
      }
      case stmt::T_DROP_PUB_SYNONYM: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_PUB_SYN);
        break;
      }
      case stmt::T_CREATE_SEQUENCE: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_SEQ, PRIV_ID_CREATE_SEQ);
        break;
      }
      case stmt::T_DROP_SEQUENCE: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_SEQ);
        break;
      }
      case stmt::T_CREATE_TRIGGER: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_TRIG, PRIV_ID_CREATE_TRIG);
        break;
      }
      case stmt::T_DROP_TRIGGER: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_TRIG);
        break;
      }
      case stmt::T_ALTER_TRIGGER: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_ALTER_ANY_TRIG);
        break;
      }
      case stmt::T_CREATE_ROLE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_ROLE);
        break;
      }
      /* xinqi to do: add admin option*/
      case stmt::T_DROP_ROLE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_ANY_ROLE);
        break;
      }
      case stmt::T_ALTER_ROLE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_ANY_ROLE);
        break;
      }
      case stmt::T_CREATE_PROFILE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_PROFILE);
        break;
      }
      case stmt::T_ALTER_PROFILE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_PROFILE);
        break;
      }
      case stmt::T_DROP_PROFILE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_PROFILE);
        break;
      }
      case stmt::T_CREATE_USER: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_USER);
        break;
      }
      case stmt::T_ALTER_USER_PROFILE:
      case stmt::T_ALTER_USER_PRIMARY_ZONE: 
      case stmt::T_ALTER_USER: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_USER);
        break;
      }
      case stmt::T_SET_PASSWORD:
      case stmt::T_LOCK_USER:
      case stmt::T_RENAME_USER: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_USER);
        break;
      }
      case stmt::T_DROP_USER: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_USER);
        break;
      }
      case stmt::T_CREATE_TYPE: {
        DEFINE_CREATE_CHECK_CMD(PRIV_ID_CREATE_ANY_TYPE, PRIV_ID_CREATE_TYPE);
        break;
      }
      case stmt::T_DROP_TYPE: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_TYPE);
        break;
      }
      case stmt::T_FLASHBACK_TABLE_FROM_RECYCLEBIN: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_TABLE);
        break;
      }
      case stmt::T_PURGE_TABLE: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_TABLE);
        break;
      }
      case stmt::T_PURGE_INDEX: {
        DEFINE_DROP_CHECK_CMD(PRIV_ID_DROP_ANY_INDEX);
        break;
      }
      case stmt::T_PURGE_RECYCLEBIN: {
        DEFINE_PURGE_CHECK_CMD(PRIV_ID_SYSDBA, PRIV_ID_PURGE_DBA_RECYCLEBIN);
        break;
      }
      case stmt::T_CREATE_OUTLINE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_ANY_OUTLINE);
        break;
      }
      case stmt::T_ALTER_OUTLINE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_ANY_OUTLINE);
        break;
      }
      case stmt::T_DROP_OUTLINE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_ANY_OUTLINE);
        break;
      }
      case stmt::T_CREATE_KEYSTORE:
      case stmt::T_ALTER_KEYSTORE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_SYSKM);
        break;
      }
      case stmt::T_CREATE_TABLESPACE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_TABLESPACE);
        break;
      }
      case stmt::T_DROP_TABLESPACE: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_TABLESPACE);
        break;
      }
      case stmt::T_ALTER_SYSTEM_SET_PARAMETER: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_ALTER_SYSTEM);
        break;
      }
      case stmt::T_CREATE_DBLINK: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_DBLINK);
        break;
      }
      case stmt::T_DROP_DBLINK: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_DBLINK);
        break;
      }
      case stmt::T_CREATE_RESTORE_POINT:
      case stmt::T_DROP_RESTORE_POINT: {
         DEFINE_PURGE_CHECK_CMD(PRIV_ID_FLASHBACK_ANY_TABLE, PRIV_ID_SELECT_ANY_DICTIONARY);
        break;
      }
      case stmt::T_CREATE_DIRECTORY: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_ANY_DIRECTORY);
        break;
      }
      case stmt::T_DROP_DIRECTORY: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_ANY_DIRECTORY);
        break;  
      }
      case stmt::T_CREATE_CONTEXT: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_CREATE_ANY_CONTEXT);
        break;
      }
      case stmt::T_DROP_CONTEXT: {
        DEFINE_PUB_CHECK_CMD(PRIV_ID_DROP_ANY_CONTEXT);
        break;  
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt type ddl priv undefined", K(stmt_type));
        break;
      }
    }
  }
  if (ret == OB_ERR_NO_PRIVILEGE) {
    ret = OB_ERR_NO_SYS_PRIVILEGE;
  }
  return ret;
}

/* ddl???????????????2????????????????????????????????????????????????
   1. ??????????????????
   2. ?????????????????? */
int ObOraSysChecker::check_ora_ddl_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const uint64_t obj_type,
    stmt::StmtType stmt_type,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    switch (stmt_type) {
      case stmt::T_ALTER_TABLE: {
        if (obj_type == static_cast<uint64_t>(ObObjectType::TABLE)) {
          DEFINE_ALTER_CHECK_CMD(PRIV_ID_ALTER_ANY_TABLE, OBJ_PRIV_ID_ALTER);
        } else {
          DEFINE_ALTER_CHECK_CMD(PRIV_ID_ALTER_ANY_INDEX, OBJ_PRIV_ID_ALTER);
        }
        break;
      }
      case stmt::T_CREATE_INDEX: {
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_CREATE_ANY_INDEX, OBJ_PRIV_ID_INDEX);
        break;
      }
      case stmt::T_FLASHBACK_TABLE_TO_SCN: {
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_FLASHBACK_ANY_TABLE, OBJ_PRIV_ID_FLASHBACK);
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_ALTER_ANY_TABLE, OBJ_PRIV_ID_ALTER);
        break;
      }
      case stmt::T_ALTER_SEQUENCE: {
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_ALTER_ANY_SEQ, OBJ_PRIV_ID_ALTER);
        break;
      }
      case stmt::T_DROP_VIEW: {
        DEFINE_DROP_VIEW_CHECK_CMD(PRIV_ID_DROP_ANY_VIEW);
        break;
      }
      case stmt::T_DROP_TABLE: {
        DEFINE_DROP_TABLE_CHECK_CMD(PRIV_ID_DROP_ANY_TABLE);
        break;
      }
      case stmt::T_TRUNCATE_TABLE: {
        DEFINE_DROP_TABLE_CHECK_CMD(PRIV_ID_DROP_ANY_TABLE);
        break;
      }
      case stmt::T_CREATE_CONTEXT: {
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_CREATE_ANY_CONTEXT, OBJ_PRIV_ID_CREATE);
        break;
      }
      case stmt::T_DROP_CONTEXT: {
        DEFINE_ALTER_CHECK_CMD(PRIV_ID_CREATE_ANY_CONTEXT, OBJ_PRIV_ID_DELETE);
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt type ddl priv undefined", K(stmt_type));
        break;
      }
    }
  }
  if (ret == OB_ERR_NO_PRIVILEGE) {
    ret = OB_ERR_NO_SYS_PRIVILEGE;
  } else if (ret == OB_ERR_EMPTY_QUERY) {
    /* ????????????????????????????????????????????? */
    switch (stmt_type) {
      case stmt::T_ALTER_TABLE:
      case stmt::T_CREATE_INDEX:
        ret = OB_TABLE_NOT_EXIST;
        break;
      case stmt::T_ALTER_SEQUENCE:
        ret = OB_ERR_SEQ_NOT_EXIST;
        break;
      default:  {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("stmt type ddl priv undefined", K(stmt_type));
        break;
      }
    }
  }
  return ret;
}

// check ora user priv
int ObOraSysChecker::check_ora_user_sys_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const ObRawPriv& priv_id,
    const ObIArray<uint64_t> &role_id_array)
{
  UNUSEDx(guard, database_name);
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    DEFINE_PUB_CHECK_CMD(priv_id);
  }
  return ret;
}

// ddl ?????????????????????????????????
int ObOraSysChecker::check_ora_ddl_ref_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &database_name,
    const uint64_t obj_id,
    const ObIArray<uint64_t> &col_ids,
    const uint64_t obj_type,
    stmt::StmtType stmt_type,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    const ObUserInfo *user_info = NULL;
    bool is_owner = false;
    OZ (guard.get_user_info(tenant_id, user_id, user_info));
    if (OB_SUCC(ret) && OB_ISNULL(user_info)) {
      ret = OB_USER_NOT_EXIST;
      LOG_WARN("user not exists", K(ret), K(user_id));
    }
    OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), database_name));
    if (false == is_owner) {
      switch (stmt_type) {
        case stmt::T_CREATE_TABLE:
        case stmt::T_ALTER_TABLE: {
          share::ObRawObjPrivArray table_privs;
          OZ (table_privs.push_back(OBJ_PRIV_ID_REFERENCES));
          OZX2 (check_obj_and_col_priv(guard, tenant_id, user_id, obj_id, obj_type,
                                      OBJ_PRIV_ID_REFERENCES, col_ids, role_id_array), 
                OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("stmt type ddl priv undefined", K(stmt_type));
          break;
        }
      }
    }
  }
  if (ret == OB_ERR_NO_PRIVILEGE || ret == OB_ERR_EMPTY_QUERY) {
    ret = OB_SUCCESS;
    bool accessible = false;
    OZ (check_access_to_obj(guard, tenant_id, user_id, obj_type, obj_id,
                            role_id_array, accessible));
    OX (ret = accessible ? OB_ERR_NO_SYS_PRIVILEGE : OB_TABLE_NOT_EXIST);
  }
  return ret;
}

// ??????user_id???????????????obj_id???????????????p1?????????????????????????????????????????????????????????????????????p1
int ObOraSysChecker::check_obj_and_col_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const uint64_t obj_id,
    const uint64_t obj_type,
    const ObRawObjPriv p1,
    const ObIArray<uint64_t> &col_ids,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObRawObjPrivArray plist;
  OZ (plist.push_back(p1));
  // 1. ??????????????????????????????????????? p1
  OZX2(check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id, OBJ_LEVEL_FOR_TAB_PRIV,
                          plist, role_id_array), OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY);
  if (OB_ERR_NO_PRIVILEGE == ret || OB_ERR_EMPTY_QUERY == ret) {
    ret = OB_SUCCESS;
    for (int i = 0; OB_SUCC(ret) && i < col_ids.count(); ++i) {
      OZX2(check_obj_plist_or(guard, tenant_id, user_id, obj_type, obj_id, col_ids.at(i),
                              plist, role_id_array), OB_ERR_NO_PRIVILEGE, OB_ERR_EMPTY_QUERY);
    }
  }
  return ret;
}

/* grant/revoke role??????????????? 
   1. grant any role??????????????? 
   2. ?????? ??????role???admin option?????? */
int ObOraSysChecker::check_ora_grant_role_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObSEArray<uint64_t, 4> &role_granted_id_array,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObRawPriv priv_need = PRIV_ID_GRANT_ANY_ROLE;
  const ObUserInfo *user_info = NULL;
  if (!is_super_user(user_id)) {
    OZX1 (check_p1(guard, tenant_id, user_id, priv_need, role_id_array), OB_ERR_NO_PRIVILEGE,
          tenant_id, user_id, priv_need);
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_SUCCESS;
      OZ (guard.get_user_info(tenant_id, user_id, user_info));
      if (OB_SUCC(ret) && NULL == user_info) {
        ret = OB_USER_NOT_EXIST;
      }
      if (OB_SUCC(ret)) {
        /* ??????schema????????????role id???admin option???????????????disable/enable
        ??????????????????
        1. ???????????????role id array
        2. ????????????role id?????????user info???????????????
        3. ??????get nth role option??????option
        4. ??????get_admin_option ??????????????? admin option */
        for (int i = 0; i < OB_SUCC(ret) && role_granted_id_array.count(); i++) {
          if (!user_info->role_exists(role_granted_id_array.at(i), ADMIN_OPTION)) {
            const ObUserInfo *user_info = NULL;            
            OZ (guard.get_user_info(tenant_id, role_granted_id_array.at(i), user_info));
            /* https://work.aone.alibaba-inc.com/issue/29301710 
            ??????dba?????????????????????ORA-01031: insufficient privileges
                     . ?????????????????? ORA-01932  ADMIN option not granted for role  */
            if (OB_SUCC(ret) && user_info != NULL) {
              if (is_ora_dba_role(role_granted_id_array.at(i))) {
                ret = OB_ERR_NO_PRIVILEGE;
              } else {
                ret = OB_ERR_ADMIN_OPTION_NOT_GRANTED_FOR_ROLE;
                LOG_USER_ERROR(OB_ERR_ADMIN_OPTION_NOT_GRANTED_FOR_ROLE, 
                              user_info->get_user_name_str().length(),
                              user_info->get_user_name_str().ptr());
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

/* grant/revoke sys priv???????????????
   1. check if has sys priv: grant any privilege
   2. check if has sys privs list with admin option */
int ObOraSysChecker::check_ora_grant_sys_priv(
  ObSchemaGetterGuard &guard,
  const uint64_t tenant_id,
  const uint64_t user_id,
  const ObRawPrivArray &sys_priv_array,
  const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  ObRawPriv priv_need = PRIV_ID_GRANT_ANY_PRIV;
  if (!is_super_user(user_id)) {
    // ??????????????? GRANT_ANY_PRIVILEGE with ADMIN option ??????????????? grant any system privilege
    OZX1(check_p1(guard, tenant_id, user_id, priv_need, role_id_array),
         OB_ERR_NO_PRIVILEGE);
    if (OB_ERR_NO_PRIVILEGE == ret) {
      ret = OB_SUCCESS;
      OZ(check_plist_and(guard, tenant_id, user_id, sys_priv_array, role_id_array, ADMIN_OPTION),
         tenant_id, user_id);
    }
    /* ??????oracle????????? */
    if (ret == OB_ERR_NO_PRIVILEGE) {
      ret = OB_ERR_NO_SYS_PRIVILEGE;
    }
  }
  return ret;
}

/* grant/revoke obj priv???????????????
   1. check if is owner, grantor == owner
   2. if not owner 
      2.1 check if has sys priv: grant any object privilege: grantor == owner
      2.2 check if has obj privs with grant option: grantor == user_id */
int ObOraSysChecker::check_ora_grant_obj_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id,
    const ObString &db_name,
    const uint64_t obj_id,
    const uint64_t obj_type,
    const ObRawObjPrivArray &table_priv_array,
    const ObSEArray<uint64_t, 4> &ins_col_ids,
    const ObSEArray<uint64_t, 4> &upd_col_ids,
    const ObSEArray<uint64_t, 4> &ref_col_ids,
    uint64_t &grantor_id_out,
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  bool is_owner;
  const ObUserInfo *user_info = NULL;
  bool has_sys_priv = false;
  ObRawPriv priv_need = PRIV_ID_GRANT_ANY_OBJECT_PRIV;
  if (!is_super_user(user_id)) {
    OZ (guard.get_user_info(tenant_id, user_id, user_info));
    if (OB_SUCC(ret) && NULL == user_info) {
      ret = OB_USER_NOT_EXIST;
      LOG_USER_ERROR(OB_USER_NOT_EXIST, db_name.length(), db_name.ptr());
    }
    OX (is_owner = ObOraPrivCheck::user_is_owner(user_info->get_user_name(), 
                                                 db_name));
    if (OB_SUCC(ret)) {
      // 1. check owner
      if (is_owner) {
        grantor_id_out = user_id;
      } else {
        // 2.1 check sys priv
        OZX1 (check_p1(guard, tenant_id, user_id, priv_need, role_id_array), OB_ERR_NO_PRIVILEGE);
        // 2.2 decide grantor_id
        if (OB_SUCC(ret) || ret == OB_ERR_NO_PRIVILEGE) {
          has_sys_priv = OB_SUCC(ret);
          ret = OB_SUCCESS;
          OZ (decide_grantor_id(guard, has_sys_priv, tenant_id, user_id, db_name, grantor_id_out));
        }
        // 2.3 check obj priv
        if (OB_SUCC(ret) && !has_sys_priv) {
          OZ (check_objplist(guard,
                            tenant_id,
                            user_id,
                            obj_id,
                            obj_type,
                            table_priv_array,
                            ins_col_ids,
                            upd_col_ids,
                            ref_col_ids,
                            role_id_array),
            tenant_id, user_id, obj_id, obj_type, table_priv_array, 
            ins_col_ids, upd_col_ids, ref_col_ids);
          /* ??????oracle???????????????????????????????????????????????? */
          if (ret == OB_ERR_NO_PRIVILEGE || ret == OB_ERR_EMPTY_QUERY) {
            ret = OB_ERR_NO_SYS_PRIVILEGE;
          }
        }
      }
    }
  } else {
    bool is_exists = false;
    uint64_t obj_owner_id = OB_INVALID_ID;
    OZ (guard.check_user_exist(tenant_id, db_name, ObString(OB_DEFAULT_HOST_NAME), 
                               is_exists, &obj_owner_id));
    if (OB_SUCC(ret) && is_exists) {
      grantor_id_out = obj_owner_id;
    }
  }
  return ret;
}

bool ObOraSysChecker::is_super_user(
    const uint64_t user_id)
{
  if (is_ora_sys_user(user_id) || is_root_user(user_id)) {
    return true;
  } else {
    return false;
  }
}

bool ObOraSysChecker::is_owner_user(
    const uint64_t user_id,
    const uint64_t owner_user_id)
{
  return (user_id == owner_user_id || owner_user_id == OB_INVALID_ID);
}

int ObOraSysChecker::check_ora_show_process_priv(
    ObSchemaGetterGuard &guard,
    const uint64_t tenant_id,
    const uint64_t user_id, 
    const ObIArray<uint64_t> &role_id_array)
{
  int ret = OB_SUCCESS;
  if (!is_super_user(user_id)) {
    DEFINE_PUB_CHECK_CMD(PRIV_ID_SHOW_PROCESS);
  }
  return ret;
}

} /* namespace sql */ 

} /* namespace oceanbase */
