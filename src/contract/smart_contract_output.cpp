#include "./smart_contract_output.hpp"
#include "../include/wren.hpp"
#include <fc/exception/exception.hpp>
#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <cctype>

using namespace graphene::chain;

const string asset_symbol = "BTS";

template<class T>
optional<T> maybe_id(const string& name_or_id)
{
    if (std::isdigit(name_or_id.front()))
    {
        try
        {
            return fc::variant(name_or_id, 1).as<T>(1);
        }
        catch (const fc::exception&)
        { // not an ID
        }
    }
    return optional<T>();
}

vector<optional<account_object>> get_accounts(database *db, const vector<account_id_type>& account_ids)
{
    vector<optional<account_object>> result; result.reserve(account_ids.size());
    std::transform(account_ids.begin(), account_ids.end(), std::back_inserter(result),
        [&](account_id_type id) -> optional<account_object> {
        if (auto o = db->find(id))
        {
            //subscribe_to_item(id);
            return *o;
        }
        return{};
    });

    return result;
}

vector<optional<account_object>> lookup_account_names(database *db, const vector<string>& account_names)
{
    const auto& accounts_by_name = db->get_index_type<account_index>().indices().get<by_name>();
    vector<optional<account_object> > result;
    result.reserve(account_names.size());
    std::transform(account_names.begin(), account_names.end(), std::back_inserter(result),
        [&accounts_by_name](const string& name) -> optional<account_object> {
        auto itr = accounts_by_name.find(name);
        return itr == accounts_by_name.end() ? optional<account_object>() : *itr;
    });

    return result;
}

account_object get_account(database *db, account_id_type id)
{
    auto rec = get_accounts(db, { id }).front();
    FC_ASSERT(rec);
    return *rec;
}
account_object get_account(database *db, string account_name_or_id)
{
    FC_ASSERT(account_name_or_id.size() > 0);

    if (auto id = maybe_id<account_id_type>(account_name_or_id))
    {
        // It's an ID
        return get_account(db, *id);
    }
    else {
        auto rec = lookup_account_names(db, { account_name_or_id }).front();
        FC_ASSERT(rec && rec->name == account_name_or_id);
        return *rec;
    }
}

account_id_type get_account_id(database *db, string account_name_or_id)
{
    return get_account(db, account_name_or_id).get_id();
}

vector<optional<asset_object>> lookup_asset_symbols(database *db, const vector<string>& symbols_or_ids)
{
    const auto& assets_by_symbol = db->get_index_type<asset_index>().indices().get<by_symbol>();
    vector<optional<asset_object> > result;
    result.reserve(symbols_or_ids.size());
    std::transform(symbols_or_ids.begin(), symbols_or_ids.end(), std::back_inserter(result),
        [&db, &assets_by_symbol](const string& symbol_or_id) -> optional<asset_object> {
        if (!symbol_or_id.empty() && std::isdigit(symbol_or_id[0]))
        {
            auto ptr = db->find(variant(symbol_or_id, 1).as<asset_id_type>(1));
            return ptr == nullptr ? optional<asset_object>() : *ptr;
        }
        auto itr = assets_by_symbol.find(symbol_or_id);
        return itr == assets_by_symbol.end() ? optional<asset_object>() : *itr;
    });
    return result;
}

vector<optional<asset_object>> get_assets(database *db, const vector<asset_id_type>& asset_ids)
{
    vector<optional<asset_object>> result; result.reserve(asset_ids.size());
    std::transform(asset_ids.begin(), asset_ids.end(), std::back_inserter(result),
        [&](asset_id_type id) -> optional<asset_object> {
        if (auto o = db->find(id))
        {
            //subscribe_to_item(id);
            return *o;
        }
        return{};
    });

    return result;
}

optional<asset_object> find_asset(database *db, asset_id_type id)
{
    auto rec = get_assets(db, { id }).front();
    if (rec)
    {
        //_asset_cache[id] = *rec;
    }
    return rec;
}

optional<asset_object> find_asset(database *db, string asset_symbol_or_id)
{
    FC_ASSERT(asset_symbol_or_id.size() > 0);

    if (auto id = maybe_id<asset_id_type>(asset_symbol_or_id))
    {
        // It's an ID
        return find_asset(db, *id);
    }
    else {
        // It's a symbol
        auto rec = lookup_asset_symbols(db, { asset_symbol_or_id }).front();
        if (rec)
        {
            if (rec->symbol != asset_symbol_or_id)
                return optional<asset_object>();

            //_asset_cache[rec->get_id()] = *rec;
        }
        return rec;
    }
}

asset_object get_asset(database *db, asset_id_type id)
{
    auto opt = find_asset(db, id);
    FC_ASSERT(opt);
    return *opt;
}

asset_object get_asset(database *db, string asset_symbol_or_id)
{
    auto opt = find_asset(db, asset_symbol_or_id);
    FC_ASSERT(opt);
    return *opt;
}

asset_id_type get_asset_id(database *db, string asset_symbol_or_id)
{
    FC_ASSERT(asset_symbol_or_id.size() > 0);
    vector<optional<asset_object>> opt_asset;
    if (std::isdigit(asset_symbol_or_id.front()))
        return fc::variant(asset_symbol_or_id, 1).as<asset_id_type>(1);
    opt_asset = lookup_asset_symbols(db, { asset_symbol_or_id });
    FC_ASSERT((opt_asset.size() > 0) && (opt_asset[0].valid()));
    return opt_asset[0]->id;
}

//fc::ecc::private_key  get_private_key(const public_key_type& id)
//{
//    auto it = _keys.find(id);
//    FC_ASSERT(it != _keys.end());
//
//    fc::optional< fc::ecc::private_key > privkey = wif_to_key(it->second);
//    FC_ASSERT(privkey);
//    return *privkey;
//}

void set_operation_fees(signed_transaction& tx, const fee_schedule& s)
{
    for (auto& op : tx.operations)
        s.set_fee(op);
}

//signed_transaction sign_transaction(database *db, signed_transaction tx, bool broadcast = false)
//{
//    set<public_key_type> pks = db->get_potential_signatures(tx);
//    flat_set<public_key_type> owned_keys;
//    owned_keys.reserve(pks.size());
//    std::copy_if(pks.begin(), pks.end(), std::inserter(owned_keys, owned_keys.end()),
//        [&](const public_key_type& pk){ return _keys.find(pk) != _keys.end(); });
//    set<public_key_type> approving_key_set = db->get_required_signatures(tx, owned_keys);
//
//    auto dyn_props = get_dynamic_global_properties();
//    tx.set_reference_block(dyn_props.head_block_id);
//
//    // first, some bookkeeping, expire old items from _recently_generated_transactions
//    // since transactions include the head block id, we just need the index for keeping transactions unique
//    // when there are multiple transactions in the same block.  choose a time period that should be at
//    // least one block long, even in the worst case.  2 minutes ought to be plenty.
//    fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
//    auto oldest_transaction_record_iter = _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
//    auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
//    _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);
//
//    uint32_t expiration_time_offset = 0;
//    for (;;)
//    {
//        tx.set_expiration(dyn_props.time + fc::seconds(30 + expiration_time_offset));
//        tx.signatures.clear();
//
//        for (const public_key_type& key : approving_key_set)
//            tx.sign(get_private_key(key), _chain_id);
//
//        graphene::chain::transaction_id_type this_transaction_id = tx.id();
//        auto iter = _recently_generated_transactions.find(this_transaction_id);
//        if (iter == _recently_generated_transactions.end())
//        {
//            // we haven't generated this transaction before, the usual case
//            recently_generated_transaction_record this_transaction_record;
//            this_transaction_record.generation_time = dyn_props.time;
//            this_transaction_record.transaction_id = this_transaction_id;
//            _recently_generated_transactions.insert(this_transaction_record);
//            break;
//        }
//
//        // else we've generated a dupe, increment expiration time and re-sign it
//        ++expiration_time_offset;
//    }
//
//    if (broadcast)
//    {
//        try
//        {
//            _remote_net_broadcast->broadcast_transaction(tx);
//        }
//        catch (const fc::exception& e)
//        {
//            elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()));
//            throw;
//        }
//    }
//
//    return tx;
//}
//
//signed_transaction transfer(database *db, string from, string to, string amount,
//    string asset_symbol, string memo, bool broadcast = false)
//{
//    try
//    {
//        fc::optional<asset_object> asset_obj = get_asset(db, asset_symbol);
//        FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));
//
//        account_object from_account = get_account(db, from);
//        account_object to_account = get_account(db, to);
//        account_id_type from_id = from_account.id;
//        account_id_type to_id = get_account_id(db, to);
//
//        transfer_operation xfer_op;
//
//        xfer_op.from = from_id;
//        xfer_op.to = to_id;
//        xfer_op.amount = asset_obj->amount_from_string(amount);
//
//        //TODO: get private key from wallet
//
//        //if (memo.size())
//        //{
//        //    xfer_op.memo = memo_data();
//        //    xfer_op.memo->from = from_account.options.memo_key;
//        //    xfer_op.memo->to = to_account.options.memo_key;
//        //    xfer_op.memo->set_message(get_private_key(from_account.options.memo_key),
//        //        to_account.options.memo_key, memo);
//        //}
//
//        signed_transaction tx;
//        tx.operations.push_back(xfer_op);
//        set_operation_fees(tx, db->get_global_properties().parameters.current_fees);
//        tx.validate();
//
//        return sign_transaction(tx, broadcast);
//    } FC_CAPTURE_AND_RETHROW((from)(to)(amount)(asset_symbol)(memo)(broadcast))
//}
//

void safeMathAdd(WrenVM *vm)
{
    uint64_t a = (uint64_t)wrenGetSlotDouble(vm, 1);
    uint64_t b = (uint64_t)wrenGetSlotDouble(vm, 2);
    uint64_t c = a + b;
    FC_ASSERT(c >= a);
    wrenSetSlotDouble(vm, 0, (double)c);
}

void safeMathSub(WrenVM *vm)
{
    uint64_t a = (uint64_t)wrenGetSlotDouble(vm, 1);
    uint64_t b = (uint64_t)wrenGetSlotDouble(vm, 2);
    FC_ASSERT(a >= b);

    wrenSetSlotDouble(vm, 0, (float)(a - b));
}

void safeMathMul(WrenVM *vm)
{
    uint64_t a = (uint64_t)wrenGetSlotDouble(vm, 1);
    uint64_t b = (uint64_t)wrenGetSlotDouble(vm, 2);
    uint64_t c = a * b;
    FC_ASSERT(a == 0 || (c / a) == b);
    wrenSetSlotDouble(vm, 0, (float)c);
}

void safeMathDiv(WrenVM *vm)
{
    uint64_t a = (uint64_t)wrenGetSlotDouble(vm, 1);
    uint64_t b = (uint64_t)wrenGetSlotDouble(vm, 2);
    FC_ASSERT(b != 0);
    wrenSetSlotDouble(vm, 0, (float)(a / b));
}

void getBalance(WrenVM *vm)
{
    const char *userId = wrenGetSlotString(vm, 1);

    UserData *userData = (UserData *)(vm->config.userData);
    database *db = (database *)userData->db;

    fc::optional<asset_object> asset_obj = get_asset(db, asset_symbol);
    FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

    account_object account = get_account(db, userId);
    asset userAsset = db->get_balance(account.get_id(), asset_obj->get_id());

    ilog("balance getter: ${b} ${a}", ("b", userAsset.amount.value)("a", asset_symbol));

    wrenSetSlotDouble(vm, 0, (float)(userAsset.amount.value));
}

void adjustBalance(WrenVM *vm)
{
    const char *userId = wrenGetSlotString(vm, 1);
    int64_t delta      = (uint64_t)wrenGetSlotDouble(vm, 2);

    UserData *userData = (UserData *)(vm->config.userData);
    database *db = (database *)userData->db;

    fc::optional<asset_object> asset_obj = get_asset(db, asset_symbol);
    FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

    account_object account = get_account(db, userId);
    //asset userAsset = db->get_balance(account.get_id(), asset_obj->get_id());

    asset deltaAsset;
    deltaAsset.amount = delta;
    deltaAsset.asset_id = asset_obj->get_id();
    db->adjust_balance(account.get_id(), deltaAsset);

    ilog("adjust balance: ${u}, ${d} ${a}", ("u", userId)("d", delta)("a", asset_symbol));
}

void getAllowed(WrenVM* vm)
{
    const char *tokenOwner = wrenGetSlotString(vm, 1);
    const char *spender = wrenGetSlotString(vm, 2);

    //TODO: get allowed withdraw from db
    wrenSetSlotDouble(vm, 0, (float)500000);
}

void adjustAllowed(WrenVM* vm)
{
    const char *tokenOwner = wrenGetSlotString(vm, 1);
    const char *spender = wrenGetSlotString(vm, 2);
    int64_t delta = (int64_t)wrenGetSlotDouble(vm, 3);

    //TODO: save allowed withdraw to db
}

void eventTransfer(WrenVM* vm)
{
    const char *from = wrenGetSlotString(vm, 1);
    const char *to   = wrenGetSlotString(vm, 2);
    uint64_t tokens  = (uint64_t)wrenGetSlotString(vm, 3);

    //TODO: save allowed withdraw to db
}

static WrenForeignMethodFn bindContractForeignMethod(WrenVM* vm, const char* module,
    const char* className, bool isStatic, const char* signature)
{
    if (!strcmp(className, "SafeMath") && isStatic)
    {
        if (!strcmp(signature, "add(_,_)"))
            return safeMathAdd;
        else if (!strcmp(signature, "sub(_,_)"))
            return safeMathSub;
        else if (!strcmp(signature, "mul(_,_)"))
            return safeMathMul;
        else if (!strcmp(signature, "div(_,_)"))
            return safeMathDiv;
    }
    else if (!strcmp(className, "Contract") && isStatic)
    {
        if (!strcmp(signature, "getBalance(_)"))
            return getBalance;
        else if (!strcmp(signature, "adjustBalance(_,_)"))
            return adjustBalance;
        if (!strcmp(signature, "getAllowed(_,_)"))
            return getAllowed;
        else if (!strcmp(signature, "adjustAllowed(_,_,_)"))
            return adjustAllowed;
        else if (!strcmp(signature, "eventTransfer(_,_,_)"))
            return eventTransfer;
    }

    FC_ASSERT(false);

    return NULL;
}

static WrenHandle* handleObj;

typedef enum {
  UNACTIVE,
  ACTIVE,
  DEAD
} SmartContractObjectState;

void reportError(WrenVM* vm, WrenErrorType type,
                        const char* module, int line, const char* message)
{
  switch (type)
  {
    case WREN_ERROR_COMPILE:
      fprintf(stderr, "[%s line %d] %s\n", module, line, message);
      break;
      
    case WREN_ERROR_RUNTIME:
      fprintf(stderr, "%s\n", message);
      break;
      
    case WREN_ERROR_STACK_TRACE:
      fprintf(stderr, "[%s line %d] in %s\n", module, line, message);
      break;
  }
}
/*
class smart_contract_obj_in_wren
{
}

class smart_contract_obj_in_cpp
{
  Address addr_of_obj;
  SmartContractObjectState = UNACTIVE;
  string *sourceCode = NULL; 
//  smart_contract_obj_in_wren *obj = NULL;//这里预留用户存储从wren里抓出来的对象（的数据部分）
}


void addressAllocate(WrenVM* vm) 
{
  const char* addr_string_in_c = wrenGetSlotString(vm, 1);
  int temp = strlen(addr_string_in_c);
  char* const address_id = (char*)wrenSetSlotNewForeign( vm, 0, 0, temp );
  for (int i = 0; i < temp; i++)
  {
      address_id[i] = addr_string_in_c[i];
      cout<<address_id[i];  
  }
  cout<<endl;

  cout<<"wrenGetSlotType(vm, 0) = "<<wrenGetSlotType(vm, 0)<<endl;
  handleObj = wrenGetSlotHandle(vm, 0);
  cout<<"handle of slot 0 in constructor function is "<<handleObj<<endl;
}

void addressFinalize(void* data) //这个需要重新写
{  
}*/
//============================================================
void implement_delegatedCall(WrenVM * vm)
{
  cout<<"hello world!"<<endl;
  wrenSetSlotString(vm, 0,"{\"a\":1,\"b\":\"2\"}");
  
}
char out[2048]={0x00};
int len =0;
void implement_wrenToString(WrenVM * vm)
{
 // cout<<"implement_wrenToString"<<endl;
 // cout<<"+++++++++++7777777+++++++++"<<endl;
 // cout<<"wrenGetSlotType(vm, 1) = "<<wrenGetSlotType(vm, 1)<<endl;
  if(len >3)
    len =0;
  char temp[128]={0x00};
  bool b_temp;
  double d_temp;
   char* s_temp; 
  WrenType slot1_Type = wrenGetSlotType(vm, 1);
  switch (slot1_Type)
  {
    case WREN_TYPE_BOOL:
    {
      b_temp = wrenGetSlotBool(vm,1);
   //   cout<<"WREN_TYPE_BOOL "<<b_temp<<endl;
      if (b_temp)
        sprintf(temp,"obj.set_state_%d(true)",len);
      else
        sprintf(temp,"obj.set_state_%d(false)",len);
      len +=1;
      wrenSetSlotDouble(vm,0,1);
      break;
    }
    case WREN_TYPE_NUM:
    {
      d_temp = wrenGetSlotDouble(vm,1);
     // cout<<"WREN_TYPE_NUM "<<d_temp<<endl;
      int d =d_temp;
      sprintf(temp,"obj.set_state_%d(%d)",len,d);
      len+=1;
      wrenSetSlotDouble(vm,0,2);
      break;
    }
    case WREN_TYPE_STRING:
    {
      s_temp = (char *)wrenGetSlotString(vm,1);
      if (s_temp[0]=='['  ){
         int a = strlen(s_temp);
         s_temp[a-1]=']';
         cout<<s_temp<<endl;
      }
      if (s_temp[0]=='{'){
        int b = strlen(s_temp);
         s_temp[b-1]='}';
         cout<<s_temp<<endl;
      }
     // cout<<"WREN_TYPE_STRING "<<s_temp<<endl;
     if (s_temp[0]=='[' || s_temp[0]=='{' ){
       sprintf(temp,"obj.set_state_%d(%s)",len,s_temp);
       cout<<temp<<endl;
     }else{
       sprintf(temp,"obj.set_state_%d(\"%s\")",len,s_temp);
     }
      
      len +=1;
      wrenSetSlotDouble(vm,0,3);
      break;
    }
    default: 
    {
      wrenSetSlotDouble(vm,0,4);
      break;
    }
    
  }
  
}

void implement_mapToGettype(WrenVM * vm)
{
  char* s_temp; 

  s_temp = (char *)wrenGetSlotString(vm,1);
  if (s_temp[0]!='\"' ){
      if(memcmp(s_temp,"false",5)==0 )
        wrenSetSlotBool(vm,0,false);
      else if(memcmp(s_temp,"true",4)==0)
        wrenSetSlotBool(vm,0,true);
      else{
        wrenSetSlotDouble(vm,0,3);
      }
  }else{
      wrenSetSlotDouble(vm,0,4);
  }
  
}

void implement_mapToGettype_ex(WrenVM * vm)
{
  char* s_temp; 

  s_temp = (char *)wrenGetSlotString(vm,1);
  if (s_temp[0]!='\"' ){
      if(memcmp(s_temp,"false",5)==0 )
        wrenSetSlotBool(vm,0,false);
      else if(memcmp(s_temp,"true",4)==0)
        wrenSetSlotBool(vm,0,true);
      else{
        wrenSetSlotDouble(vm,0,3);
      }
  }else{
      wrenSetSlotDouble(vm,0,4);
  }
  
}

char* state;
void implement_setStateToString(WrenVM * vm)
{
  state = (char *)wrenGetSlotString(vm,1);
}

void implement_getStateFromString(WrenVM * vm)
{
  wrenSetSlotString(vm, 0,state);
}

void implement_getStateFromString_ex(WrenVM * vm)
{
  wrenSetSlotString(vm, 0,state);
}
//============================================================
void implement_saveObj(WrenVM * vm)
{

  cout<<"saveing object!"<<endl;
  cout<<"wrenGetSlotType(vm, 0) = "<<wrenGetSlotType(vm, 0)<<endl;
  cout<<"wrenGetSlotCount(vm) = "<<wrenGetSlotCount(vm)<<endl;

  cout<<"&&&&&&&&&&&&&&&&&&&&&&&&&&&"<<endl;
  WrenHandle *method_temp = wrenMakeCallHandle(vm, "getMemberVariable()");
  const char * str = wrenGetSlotString(vm, 0);
  cout<<"&&&)))))))))))))))))))((((((((((((((&&&&&&&"<<endl;
  cout<<str[0]<<str[1]<<str[2]<<str[3]<<str[4]<<str[5]<<endl;
//  handleObj = wrenGetSlotHandle(vm, 0);
//  WrenHandle* handleObj;
//  cout<<"wrenGetSlotType(vm, 1) = "<<wrenGetSlotType(vm, 1)<<endl;
//  cout<<"handle of slot 0 in saveObj function is "<<handleObj<<endl;
 // cout<<"complete saveing object!"<<endl;
}

void implement_unsaveObj(WrenVM * vm)
{
  cout<<"unsaveing object! That means that a object instant is reimplemented into wren"<<endl;
//  wrenSetSlotHandle(vm, 0, handleObj);
  cout<<"complete unsaveing object!"<<endl;
}

//============================================================
void implement_activateSmartContractObjInCpp(WrenVM * vm)
{
  //*****************************************
}
//============================================================
static void write(WrenVM* vm, const char* text)
{
  cout<<text<<endl;
}

//============================================================


/*
WrenForeignClassMethods bindForeignClass_selfDef(WrenVM* vm, const char* module, const char* className) 
{ 
  WrenForeignClassMethods methods;

  if (strcmp(className, "Address") == 0) 
  { 
    methods.allocate = addressAllocate; 
    methods.finalize = addressFinalize; 
  } 
//  else if (strcmp(className, "Interface") == 0)
//  {
//    methods.allocate = interfaceAllocate; 
//    methods.finalize = interfaceFinalize;
//  }
  else 
  { 
    // Unknown class.
    methods.allocate = NULL; 
    methods.finalize = NULL; 
  }

  return methods; 
}
*/

WrenForeignMethodFn bindForeignMethod_selfDef(WrenVM* vm, const char* module, 
    const char* className, bool isStatic, const char* signature) 
{ 
  if (strcmp(className, "Address") == 0) 
  { 
    if (!isStatic && strcmp(signature, "delegateCall()") == 0) 
    { 
      return implement_delegatedCall; 
    }
    else if (!isStatic && strcmp(signature, "saveObj()") == 0)
    {
      return implement_saveObj;
    }
    else if (!isStatic && strcmp(signature, "unsaveObj()") == 0)
    {
      return implement_unsaveObj;
    }
  }

  if (strcmp(className, "Interface") == 0) 
  { 
    if (!isStatic && strcmp(signature, "activateSmartContractObjInCpp()") == 0)///////////////// 
    { 
      return implement_activateSmartContractObjInCpp; 
    } 
  }

  if (strcmp(className, "A") == 0)
  {
    if (!isStatic && strcmp(signature, "wrenToString(_)") == 0)///////////////// 
    { 
      return implement_wrenToString; 
    }
    else if(!isStatic && strcmp(signature, "mapToGettype(_)") == 0)
    {
      return implement_mapToGettype;
    } 
  }

  if (strcmp(className, "Contract") == 0)
  {
    if (!isStatic && strcmp(signature, "wrenToString(_)") == 0)///////////////// 
    { 
      return implement_wrenToString; 
    }
    else if(!isStatic && strcmp(signature, "mapToGettype(_)") == 0)
    {
      return implement_mapToGettype;
    }
    else if(/*!isStatic && */strcmp(signature, "mapToGettype_ex(_)") == 0)
    {
      return implement_mapToGettype_ex;
    }
    else if(!isStatic && strcmp(signature, "setStateToString(_)") == 0)
    {
      return implement_setStateToString;
    }
    else if(!isStatic && strcmp(signature, "getStateFromString()") == 0)
    {
      return implement_getStateFromString;
    }
     else if(/*!isStatic && */strcmp(signature, "getStateFromString_ex()") == 0)
    {
      return implement_getStateFromString_ex;
    }

  }
  return NULL; 

}

extern char * rootDirectory;

static char* readFile(const char* path)
{
  FILE* file = fopen(path, "rb");
  if (file == NULL) return NULL;
  
  // Find out how big the file is.
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);
  
  // Allocate a buffer for it.
  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL)
  {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  
  // Read the entire file.
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize)
  {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  
  // Terminate the string.
  buffer[bytesRead] = '\0';
  
  fclose(file);
  return buffer;
}

static char* wrenFilePath(const char* name)
{
  // The module path is relative to the root directory and with ".wren".
  size_t rootLength = rootDirectory == NULL ? 0 : strlen(rootDirectory);
  size_t nameLength = strlen(name);
  size_t pathLength = rootLength + nameLength + 5;
  char* path = (char*)malloc(pathLength + 1);
  
  if (rootDirectory != NULL)
  {
    memcpy(path, rootDirectory, rootLength);
  }
  
  memcpy(path + rootLength, name, nameLength);
  memcpy(path + rootLength + nameLength, ".wren", 5);
  path[pathLength] = '\0';
  
  return path;
}

static char* readModule(WrenVM* vm, const char* module)
{
 // char* source = readBuiltInModule(module);
//  if (source != NULL) return source;
  
  // First try to load the module with a ".wren" extension.
  char* modulePath = wrenFilePath(module);
  char* moduleContents = readFile(modulePath);
  free(modulePath);
  
  if (moduleContents != NULL) return moduleContents;
  
  // If no contents could be loaded treat the module name as specifying a
  // directory and try to load the "module.wren" file in the directory.
  size_t moduleLength = strlen(module);
  size_t moduleDirLength = moduleLength + 7;
  char* moduleDir = (char*)malloc(moduleDirLength + 1);
  memcpy(moduleDir, module, moduleLength);
  memcpy(moduleDir + moduleLength, "/module", 7);
  moduleDir[moduleDirLength] = '\0';
  
  char* moduleDirPath = wrenFilePath(moduleDir);
  free(moduleDir);
  
  moduleContents = readFile(moduleDirPath);
  free(moduleDirPath);
  
  return moduleContents;
}

WrenVM* vm1;
   

int smart_contract_output()
{
    //string method ="member1_add(20)";
    //string out = InvokeSmartContract("", method, "");
   
    //wrenFreeVM(vm1);
    
    return 0;
}

void Trim(char* str) 
{
	char *ptail, *p;
	p = str;
	while(*p && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++;//\n是换行，光标下移一格;\r是回车，使光标到行首；\t是Tab键;
	if (!*p) 
	{
		*str = 0;
		return ;
	}
	
	ptail = p+strlen(p)-1;
	while(ptail > p && (*ptail == ' ' || *ptail == '\t' || *ptail == '\n' || *ptail == '\r')) 
	{
		*ptail = 0;
		ptail--;
	}
	
	int L = strlen(p);
	memmove(str, p, L);
	str[L] = 0;
}

string run_wren_vm_when_activating_smart_contract(string input_source_code,string initargu)//首次激活合约时调用的接口函数 
{
  //add for test

    UserData userData;
    userData.size = sizeof(userData);
    userData.vmMode = VM_MODE_INTERPRET;
    
    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.bindForeignMethodFn = bindForeignMethod_selfDef;
    config.writeFn             = write;
    config.errorFn             = reportError;
    config.initialHeapSize     = 1024 * 1024 * 10;
    config.loadModuleFn        = readModule;
    config.userData            = &userData;

    WrenVM *vm = wrenNewVM(&config);

     string temp = input_source_code;
     int fclass1 =temp.find("class ");
     int fclass2 =temp.find("is",fclass1+6);
     string classname = temp.substr(fclass1+6,fclass2-fclass1-6);
     int fconstruct1 =temp.find("construct ");
     int fconstruct2 =temp.find("(",fconstruct1+10);
     string constructname =temp.substr(fconstruct1+10,fconstruct2-fconstruct1-10);
     Trim((char *)classname.c_str());
     Trim((char *)constructname.c_str());
     string init = (char *)classname.c_str();
     init +="."+constructname+"([";
     init +=initargu;
     init +="])";
     string code_pre ="import \"contract\" for Contract\n";
     code_pre += input_source_code;
     code_pre += "\nvar obj =";
     code_pre += init;
     code_pre += "\nobj.save_member_variables_into_c()\n";
     char* data=(char *)code_pre.c_str();
     cout<< data <<endl;
     WrenInterpretResult result = wrenInterpret(vm, "main", data);
     wrenFreeVM(vm);
     string output;
     if(result != 0)
     {
       if(result==1)
          output="error:WREN_RESULT_COMPILE_ERROR";
       else
          output="error:WREN_RESULT_RUNTIME_ERROR";
       cout<<"output = "<<output<<endl;
       return output;
     }
     output = state;
     cout<<"output = "<<output<<endl;
     return output; 
}

string smart_contract_call_evaluator::invoke_smart_contract(string input_source_code,
    string contranct_method_and_parameter,
    string starting_state)
{
    UserData userData;
    userData.size   = sizeof(userData);
    userData.vmMode = VM_MODE_INTERPRET;
    userData.db     = &db();

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.bindForeignMethodFn = bindContractForeignMethod;
    config.writeFn             = write;
    config.errorFn             = reportError;
    config.initialHeapSize     = 1024 * 1024 * 10;
    config.loadModuleFn        = readModule;
    config.userData            = &userData;

    WrenVM *vm = wrenNewVM(&config);

    if (userData.vmMode == VM_MODE_INTERPRET)
    {        
        std::string sourceCode;
        sourceCode += input_source_code;
        sourceCode += "\nERC20Simple.transfer(\"1.2.17\", \"1.2.18\", 1)\n";
        WrenInterpretResult result = wrenInterpret(vm, "main", sourceCode.c_str());
    }
    else
    {
        //TODO: run byte code through ABI-based function call, and get ret value of function call.
        FC_ASSERT(false);
    }

    wrenFreeVM(vm);

    //TODO: get ret value of function call.
    return "";
}