#pragma once

#include <inery/asset.hpp>
#include <inery/multi_index.hpp>
#include <inery/privileged.hpp>
#include <inery/singleton.hpp>
#include <inery/system.hpp>
#include <inery/time.hpp>

#include <inery.system/exchange_state.hpp>
#include <inery.system/native.hpp>

#include <deque>
#include <optional>
#include <string>
#include <type_traits>


namespace inerysystem {

   using inery::asset;
   using inery::block_timestamp;
   using inery::check;
   using inery::const_mem_fun;
   using inery::datastream;
   using inery::indexed_by;
   using inery::name;
   using inery::same_payer;
   using inery::symbol;
   using inery::symbol_code;
   using inery::time_point;
   using inery::time_point_sec;
   using inery::unsigned_int;

   template<typename E, typename F>
   static inline auto has_field( F flags, E field )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, bool>
   {
      return ( (flags & static_cast<F>(field)) != 0 );
   }

   template<typename E, typename F>
   static inline auto set_field( F flags, E field, bool value = true )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, F >
   {
      if( value )
         return ( flags | static_cast<F>(field) );
      else
         return ( flags & ~static_cast<F>(field) );
   }

   static constexpr uint32_t seconds_per_year      = 52 * 7 * 24 * 3600;
   static constexpr uint32_t seconds_per_day       = 24 * 3600;
   static constexpr uint32_t seconds_per_hour      = 3600;
   static constexpr int64_t  useconds_per_year     = int64_t(seconds_per_year) * 1000'000ll;
   static constexpr int64_t  useconds_per_day      = int64_t(seconds_per_day) * 1000'000ll;
   static constexpr int64_t  useconds_per_hour     = int64_t(seconds_per_hour) * 1000'000ll;
   static constexpr uint32_t blocks_per_day        = 2 * seconds_per_day; // half seconds per day

   // static constexpr int64_t  min_activated_stake   = 150'000'000'0000;
   static constexpr int64_t  min_activated_stake   = 1'0000;
   static constexpr int64_t  mem_gift_bytes        = 1400;
   static constexpr int64_t  min_pervote_daily_pay = 100'0000;
   static constexpr uint32_t refund_delay_sec      = 3 * seconds_per_day;

   static constexpr int64_t  inflation_precision           = 100;     // 2 decimals
   static constexpr int64_t  default_annual_rate           = 500;     // 5% annual rate
   static constexpr int64_t  pay_factor_precision          = 10000;
   static constexpr int64_t  default_inflation_pay_factor  = 50000;   // producers pay share = 10000 / 50000 = 20% of the inflation
   static constexpr int64_t  default_votepay_factor        = 40000;   // per-block pay share = 10000 / 40000 = 25% of the producer pay

   // Defines new global state parameters.
   struct [[inery::table("global"), inery::contract("inery.system")]] inery_global_state : inery::blockchain_parameters {
      uint64_t free_mem()const { return max_mem_size - total_mem_bytes_reserved; }

      uint64_t             max_mem_size = 64ll*1024 * 1024 * 1024;
      uint64_t             total_mem_bytes_reserved = 0;
      int64_t              total_mem_stake = 0;

      block_timestamp      last_producer_schedule_update;
      time_point           last_pervote_bucket_fill;
      int64_t              pervote_bucket = 0;
      int64_t              perblock_bucket = 0;
      uint32_t             total_unpaid_blocks = 0; /// all blocks which have been produced but not paid
      int64_t              total_activated_stake = 0;
      time_point           thresh_activated_stake_time;
      uint16_t             last_producer_schedule_size = 0;
      double               total_producer_vote_weight = 0; /// the sum of all producer votes
      block_timestamp      last_name_close;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE_DERIVED( inery_global_state, inery::blockchain_parameters,
                                (max_mem_size)(total_mem_bytes_reserved)(total_mem_stake)
                                (last_producer_schedule_update)(last_pervote_bucket_fill)
                                (pervote_bucket)(perblock_bucket)(total_unpaid_blocks)(total_activated_stake)(thresh_activated_stake_time)
                                (last_producer_schedule_size)(total_producer_vote_weight)(last_name_close) )
   };

   // Defines new global state parameters added after version 1.0
   struct [[inery::table("global2"), inery::contract("inery.system")]] inery_global_state2 {
      inery_global_state2(){}

      uint16_t          new_mem_per_block = 0;
      block_timestamp   last_mem_increase;
      block_timestamp   last_block_num; /* deprecated */
      double            total_producer_votepay_share = 0;
      uint8_t           revision = 0; ///< used to track version updates in the future.

      INELIB_SERIALIZE( inery_global_state2, (new_mem_per_block)(last_mem_increase)(last_block_num)
                        (total_producer_votepay_share)(revision) )
   };

   // Defines new global state parameters added after version 1.3.0
   struct [[inery::table("global3"), inery::contract("inery.system")]] inery_global_state3 {
      inery_global_state3() { }
      time_point        last_vpay_state_update;
      double            total_vpay_share_change_rate = 0;

      INELIB_SERIALIZE( inery_global_state3, (last_vpay_state_update)(total_vpay_share_change_rate) )
   };

   // Defines new global state parameters to store inflation rate and distribution
   struct [[inery::table("global4"), inery::contract("inery.system")]] inery_global_state4 {
      inery_global_state4() { }
      double   continuous_rate;
      int64_t  inflation_pay_factor;
      int64_t  votepay_factor;

      INELIB_SERIALIZE( inery_global_state4, (continuous_rate)(inflation_pay_factor)(votepay_factor) )
   };

   // Defines `producer_info` structure to be stored in `producer_info` table, added after version 1.0
   struct [[inery::table, inery::contract("inery.system")]] producer_info {
      name                  owner;
      double                total_votes = 0;
      inery::public_key     producer_key; /// a packed public key object
      bool                  is_active = true;
      std::string           url;
      uint32_t              unpaid_blocks = 0;
      time_point            last_claim_time;
      uint16_t              location = 0;
      bool                  is_producer = false;
      uint64_t primary_key()const { return owner.value;                             }
      double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
      bool     active()const      { return is_active;                               }
      void     deactivate()       { producer_key = public_key(); is_active = false; }
      bool     active_producer()const      { return is_producer;                    }
      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( producer_info, (owner)(total_votes)(producer_key)(is_active)(url)
                        (unpaid_blocks)(last_claim_time)(location) )
   };

   // Defines new producer info structure to be stored in new producer info table, added after version 1.3.0
   struct [[inery::table, inery::contract("inery.system")]] producer_info2 {
      name            owner;
      double          votepay_share = 0;
      time_point      last_votepay_share_update;

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( producer_info2, (owner)(votepay_share)(last_votepay_share_update) )
   };

   // Voter info. Voter info stores information about the voter:
   // - `owner` the voter
   // - `proxy` the proxy set by the voter, if any
   // - `producers` the producers approved by this voter if no proxy set
   // - `staked` the amount staked
   struct [[inery::table, inery::contract("inery.system")]] voter_info {
      name                owner;     /// the voter
      name                proxy;     /// the proxy set by the voter, if any
      std::vector<name>   producers; /// the producers approved by this voter if no proxy set
      int64_t             staked = 0;

      //  Every time a vote is cast we must first "undo" the last vote weight, before casting the
      //  new vote weight.  Vote weight is calculated as:
      //  stated.amount * 2 ^ ( weeks_since_launch/weeks_per_year)
      double              last_vote_weight = 0; /// the vote weight cast the last time the vote was updated

      // Total vote weight delegated to this voter.
      double              proxied_vote_weight= 0; /// the total vote weight delegated to this voter as a proxy
      bool                is_proxy = 0; /// whether the voter is a proxy for others


      uint32_t            flags1 = 0;
      uint32_t            reserved2 = 0;
      inery::asset        reserved3;

      uint64_t primary_key()const { return owner.value; }

      enum class flags1_fields : uint32_t {
         mem_managed = 1,
         net_managed = 2,
         cpu_managed = 4
      };

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( voter_info, (owner)(proxy)(producers)(staked)(last_vote_weight)(proxied_vote_weight)(is_proxy)(flags1)(reserved2)(reserved3) )
   };


   typedef inery::multi_index< "voters"_n, voter_info >  voters_table;


   typedef inery::multi_index< "producers"_n, producer_info,
                               indexed_by<"prototalvote"_n, const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                             > producers_table;

   typedef inery::multi_index< "producers2"_n, producer_info2 > producers_table2;


   typedef inery::singleton< "global"_n, inery_global_state >   global_state_singleton;

   typedef inery::singleton< "global2"_n, inery_global_state2 > global_state2_singleton;

   typedef inery::singleton< "global3"_n, inery_global_state3 > global_state3_singleton;

   typedef inery::singleton< "global4"_n, inery_global_state4 > global_state4_singleton;

   struct [[inery::table, inery::contract("inery.system")]] user_resources {
      name          owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       mem_bytes = 0;
      uint64_t memory_return() const {return mem_bytes; }
      bool is_empty()const { return net_weight.amount == 0 && cpu_weight.amount == 0 && mem_bytes == 0; }
      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(mem_bytes) )
   };

   // Every user 'from' has a scope/table that uses every receipinet 'to' as the primary key.
   struct [[inery::table, inery::contract("inery.system")]] delegated_bandwidth {
      name          from;
      name          to;
      asset         net_weight;
      asset         cpu_weight;

      bool is_empty()const { return net_weight.amount == 0 && cpu_weight.amount == 0; }
      uint64_t  primary_key()const { return to.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight) )

   };

   struct [[inery::table, inery::contract("inery.system")]] refund_request {
      name            owner;
      time_point_sec  request_time;
      inery::asset    net_amount;
      inery::asset    cpu_amount;

      bool is_empty()const { return net_amount.amount == 0 && cpu_amount.amount == 0; }
      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      INELIB_SERIALIZE( refund_request, (owner)(request_time)(net_amount)(cpu_amount) )
   };
   struct [[inery::table, inery::contract("inery.system")]] exchange_state {
      asset    supply;

      struct connector {
         asset balance;
         double weight = 1;

         INELIB_SERIALIZE( connector, (balance)(weight) )
      };

      connector base;
      connector quote;

      uint64_t primary_key()const { return supply.symbol.raw(); }

      asset convert_to_exchange( connector& reserve, const asset& payment );
      asset convert_from_exchange( connector& reserve, const asset& tokens );
      asset convert( const asset& from, const symbol& to );
      asset direct_convert( const asset& from, const symbol& to );

      static int64_t get_bancor_output( int64_t inp_reserve,
                                        int64_t out_reserve,
                                        int64_t inp );
      static int64_t get_bancor_input( int64_t out_reserve,
                                       int64_t inp_reserve,
                                       int64_t out );

      INELIB_SERIALIZE( exchange_state, (supply)(base)(quote) )
   };


   typedef inery::multi_index< "userres"_n, user_resources >      user_resources_table;
   typedef inery::multi_index< "delband"_n, delegated_bandwidth > del_bandwidth_table;
   typedef inery::multi_index< "refunds"_n, refund_request >      refunds_table;


   class [[inery::contract("inery.system")]] system_contract : public native {

      private:
         voters_table             _voters;
         producers_table          _producers;
         producers_table2         _producers2;
         global_state_singleton   _global;
         global_state2_singleton  _global2;
         global_state3_singleton  _global3;
         global_state4_singleton  _global4;
         inery_global_state       _gstate;
         inery_global_state2      _gstate2;
         inery_global_state3      _gstate3;
         inery_global_state4      _gstate4;
         memmarket                _memmarket;


      public:
         static constexpr inery::name active_permission{"active"_n};
         static constexpr inery::name token_account{"inery.token"_n};
         static constexpr inery::name mem_account{"inery.mem"_n};
         static constexpr inery::name memfee_account{"inery.memfee"_n};
         static constexpr inery::name stake_account{"inery.stake"_n};
         static constexpr inery::name bpay_account{"inery.bpay"_n};
         static constexpr inery::name vpay_account{"inery.vpay"_n};
         static constexpr inery::name names_account{"inery.names"_n};
         static constexpr inery::name saving_account{"inery.saving"_n};
         static constexpr inery::name null_account{"inery.null"_n};
         static constexpr inery::name account_creator{"createacc"_n};
         static constexpr symbol memcore_symbol = symbol(symbol_code("MEMCORE"), 4);
         static constexpr symbol mem_symbol     = symbol(symbol_code("MEM"), 0);

         system_contract( name s, name code, datastream<const char*> ds );
         ~system_contract();

          // Returns the core symbol by system account name
          // @param system_account - the system account to get the core symbol for.
         static symbol get_core_symbol( name system_account = "inery"_n ) {
            memmarket rm(system_account, system_account.value);
            const static auto sym = get_core_symbol( rm );
            return sym;
         }

         // Actions:
         /**
          * The Init action initializes the system contract for a version and a symbol.
          * Only succeeds when:
          * - version is 0 and
          * - symbol is found and
          * - system token supply is greater than 0,
          * - and system contract wasn’t already been initialized.
          *
          * @param version - the version, has to be 0,
          * @param core - the system symbol.
          */
         [[inery::action]]
         void init( unsigned_int version, const symbol& core );

         /**
          * On block action. This special action is triggered when a block is applied by the given producer
          * and cannot be generated from any other source. It is used to pay producers and calculate
          * missed blocks of other producers. Producer pay is deposited into the producer's stake
          * balance and can be withdrawn over time. If blocknum is the start of a new round this may
          * update the active producer config from the producer votes.
          *
          * @param header - the block header produced.
          */
         [[inery::action]]
         void onblock( ignore<block_header> header );

         /**
          * Set account limits action sets the resource limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param mem_bytes - mem limit in absolute bytes,
          * @param net_weight - fractionally proportionate net limit of available resources based on (weight / total_weight_of_all_accounts),
          * @param cpu_weight - fractionally proportionate cpu limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[inery::action]]
         void setalimits( const name& account, int64_t mem_bytes, int64_t net_weight, int64_t cpu_weight );

         /**
          * Set account MEM limits action, which sets the MEM limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param mem_bytes - mem limit in absolute bytes.
          */
         [[inery::action]]
         void setacctram( const name& account, const std::optional<int64_t>& mem_bytes );

         /**
          * Set account NET limits action, which sets the NET limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param net_weight - fractionally proportionate net limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[inery::action]]
         void setacctnet( const name& account, const std::optional<int64_t>& net_weight );

         /**
          * Set account CPU limits action, which sets the CPU limits of an account
          *
          * @param account - name of the account whose resource limit to be set,
          * @param cpu_weight - fractionally proportionate cpu limit of available resources based on (weight / total_weight_of_all_accounts).
          */
         [[inery::action]]
         void setacctcpu( const name& account, const std::optional<int64_t>& cpu_weight );


         /**
          * Activates a protocol feature
          *
          * @param feature_digest - hash of the protocol feature to activate.
          */
         [[inery::action]]
         void activate( const inery::checksum256& feature_digest );

         // functions defined in delegate_bandwidth.cpp

         /**
          * Delegate bandwidth and/or cpu action. Stakes SYS from the balance of `from` for the benefit of `receiver`.
          *
          * @param from - the account to delegate bandwidth from, that is, the account holding
          *    tokens to be staked,
          * @param receiver - the account to delegate bandwith to, that is, the account to
          *    whose resources staked tokens are added
          * @param stake_net_quantity - tokens staked for NET bandwidth,
          * @param stake_cpu_quantity - tokens staked for CPU bandwidth,
          * @param transfer - if true, ownership of staked tokens is transfered to `receiver`.
          *
          * @post All producers `from` account has voted for will have their votes updated immediately.
          */
         [[inery::action]]
         void delegatebw( const name& from, const name& receiver,
                          const asset& stake_net_quantity, const asset& stake_cpu_quantity, bool transfer );

         

         /**
          * Undelegate bandwitdh action. Decreases the total tokens delegated by `from` to `receiver` and/or
          * frees the memory associated with the delegation if there is nothing
          * left to delegate.
          * This will cause an immediate reduction in net/cpu bandwidth of the
          * receiver.
          * A transaction is scheduled to send the tokens back to `from` after
          * the staking period has passed. If existing transaction is scheduled, it
          * will be canceled and a new transaction issued that has the combined
          * undelegated amount.
          * The `from` account loses voting power as a result of this call and
          * all producer tallies are updated.
          *
          * @param from - the account to undelegate bandwidth from, that is,
          *    the account whose tokens will be unstaked,
          * @param receiver - the account to undelegate bandwith to, that is,
          *    the account to whose benefit tokens have been staked,
          * @param unstake_net_quantity - tokens to be unstaked from NET bandwidth,
          * @param unstake_cpu_quantity - tokens to be unstaked from CPU bandwidth,
          *
          * @post Unstaked tokens are transferred to `from` liquid balance via a
          *    deferred transaction with a delay of 3 days.
          * @post If called during the delay period of a previous `undelegatebw`
          *    action, pending action is canceled and timer is reset.
          * @post All producers `from` account has voted for will have their votes updated immediately.
          * @post Bandwidth and storage for the deferred transaction are billed to `from`.
          */
         [[inery::action]]
         void undelegatebw( const name& from, const name& receiver,
                            const asset& unstake_net_quantity, const asset& unstake_cpu_quantity );

         /**
          * Buy mem action. Increases receiver's mem quota based upon current price and quantity of
          * tokens provided. An inline transfer from receiver to system contract of
          * tokens will be executed.
          *
          * @param payer - the mem buyer,
          * @param receiver - the mem receiver,
          * @param quant - the quntity of tokens to buy mem with.
          */
         [[inery::action]]
         void buyresources( const name& payer, const name& receiver, const asset& quant );

         /**
          * Buy a specific amount of mem bytes action. Increases receiver's mem in quantity of bytes provided.
          * An inline transfer from receiver to system contract of tokens will be executed.
          *
          * @param payer - the mem buyer,
          * @param receiver - the mem receiver,
          * @param bytes - the quntity of mem to buy specified in bytes.
          */
         [[inery::action]]
         void buymembytes( const name& payer, const name& receiver, uint32_t bytes );

         /**
          * Sell mem action. Reduces quota by bytes and then performs an inline transfer of tokens
          * to receiver based upon the average purchase price of the original quota.
          *
          * @param account - the mem seller account,
          * @param bytes - the amount of mem to sell in bytes.
          */
         [[inery::action]]
         void sellres( const name& account, int64_t bytes );

         /**
          * Refund action. This action is called after the delegation-period to claim all pending
          * unstaked tokens belonging to owner.
          *
          * @param owner - the owner of the tokens claimed.
          */
         [[inery::action]]
         void refund( const name& owner );

         // functions defined in voting.cpp

         /**
          * Register producer action. Register producer action, indicates that a particular account wishes to become a producer,
          * this action will create a `producer_config` and a `producer_info` object for `producer` scope
          * in producers tables.
          *
          * @param producer - account registering to be a producer candidate,
          * @param producer_key - the public key of the block producer, this is the key used by block producer to sign blocks,
          * @param url - the url of the block producer, normally the url of the block producer presentation website,
          * @param location - is the country code as defined in the ISO 3166, https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
          *
          * @pre Producer is not already registered
          * @pre Producer to register is an account
          * @pre Authority of producer to register
          */
         [[inery::action]]
         void regproducer( const name& producer, const public_key& producer_key, const std::string& url, uint16_t location );

         /**
          * Unregister producer action. Deactivate the block producer with account name `producer`.
          *
          * Deactivate the block producer with account name `producer`.
          * @param producer - the block producer account to unregister.
          */
         [[inery::action]]
         void unregprod( const name& producer );

         /**
          * Set mem action sets the mem supply
          * @param max_mem_size - the amount of mem supply to set.
          */
         [[inery::action]]
         void setmem( uint64_t max_mem_size );

         /**
          * Set mem rate action. Sets the rate of increase of MEM in bytes per block. It is capped by the uint16_t to
          * a maximum rate of 3 TB per year. If update_mem_supply hasn't been called for the most recent block,
          * then new mem will be allocated at the old rate up to the present block before switching the rate.
          *
          * @param bytes_per_block - the amount of bytes per block increase to set.
          */
         [[inery::action]]
         void setmemrate( uint16_t bytes_per_block );

         /**
          * Vote producer action. Votes for a set of producers. This action updates the list of `producers` voted for,
          * for `voter` account. If voting for a `proxy`, the producer votes will not change until the
          * proxy updates their own vote. Voter can vote for a proxy __or__ a list of at most 30 producers.
          * Storage change is billed to `voter`.
          *
          * @param voter - the account to change the voted producers for,
          * @param proxy - the proxy to change the voted producers for,
          * @param producers - the list of producers to vote for, a maximum of 30 producers is allowed.
          *
          * @pre Producers must be sorted from lowest to highest and must be registered and active
          * @pre If proxy is set then no producers can be voted for
          * @pre If proxy is set then proxy account must exist and be registered as a proxy
          * @pre Every listed producer or proxy must have been previously registered
          * @pre Voter must authorize this action
          * @pre Voter must have previously staked some INE for voting
          * @pre Voter->staked must be up to date
          *
          * @post Every producer previously voted for will have vote reduced by previous vote weight
          * @post Every producer newly voted for will have vote increased by new vote amount
          * @post Prior proxy will proxied_vote_weight decremented by previous vote weight
          * @post New proxy will proxied_vote_weight incremented by new vote weight
          */
         [[inery::action]]
         void makeprod( const name& voter, const name& proxy, const std::vector<name>& producers );

         /**
          * Register proxy action. Set `proxy` account as proxy.
          * An account marked as a proxy can vote with the weight of other accounts which
          * have selected it as a proxy. Other accounts must refresh their makeprod to
          * update the proxy's weight.
          * Storage change is billed to `proxy`.
          *
          * @param rpoxy - the account registering as voter proxy (or unregistering),
          * @param isproxy - if true, proxy is registered; if false, proxy is unregistered.
          *
          * @pre Proxy must have something staked (existing row in voters table)
          * @pre New state must be different than current state
          */
         [[inery::action]]
         void regproxy( const name& proxy, bool isproxy );

         /**
          * Set the blockchain parameters Set the blockchain parameters. By tunning these parameters a degree of
          * customization can be achieved.
          * @param params - New blockchain parameters to set.
          */
         [[inery::action]]
         void setparams( const inery::blockchain_parameters& params );

         /**
          * Claim rewards action. Claim block producing and vote rewards.
          * @param owner - producer account claiming per-block and per-vote rewards.
          */
         [[inery::action]]
         void claimrewards( const name& owner );

         /**
          * Set privilege status for an account. Allows to set privilege status for an account (turn it on/off).
          * @param account - the account to set the privileged status for.
          * @param is_priv - 0 for false, > 0 for true.
          */
         [[inery::action]]
         void setpriv( const name& account, uint8_t is_priv );

         /**
          * Remove producer action. Deactivates a producer by name, if not found asserts.
          * @param producer - the producer account to deactivate.
          */
         [[inery::action]]
         void rmvproducer( const name& producer );


         /**
          * Change the annual inflation rate of the core token supply and specify how
          *          the new issued tokens will be distributed based on the following structure.

          *
          * @param annual_rate - Annual inflation rate of the core token supply.
          *     (eg. For 5% Annual inflation => annual_rate=500
          *          For 1.5% Annual inflation => annual_rate=150
          *
          * @param inflation_pay_factor - Inverse of the fraction of the inflation used to reward block producers.
          *     The remaining inflation will be sent to the `inery.saving` account.
          *     (eg. For 20% of inflation going to block producer rewards   => inflation_pay_factor = 50000
          *          For 100% of inflation going to block producer rewards  => inflation_pay_factor = 10000).
          *
          * @param votepay_factor - Inverse of the fraction of the block producer rewards to be distributed proportional to blocks produced.
          *     The remaining rewards will be distributed proportional to votes received.
          *     (eg. For 25% of block producer rewards going towards block pay => votepay_factor = 40000
          *          For 75% of block producer rewards going towards block pay => votepay_factor = 13333).
          */
         [[inery::action]]
         void setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor );

         using init_action = inery::action_wrapper<"init"_n, &system_contract::init>;
         using setacctram_action = inery::action_wrapper<"setacctram"_n, &system_contract::setacctram>;
         using setacctnet_action = inery::action_wrapper<"setacctnet"_n, &system_contract::setacctnet>;
         using setacctcpu_action = inery::action_wrapper<"setacctcpu"_n, &system_contract::setacctcpu>;
         using activate_action = inery::action_wrapper<"activate"_n, &system_contract::activate>;
         using delegatebw_action = inery::action_wrapper<"delegatebw"_n, &system_contract::delegatebw>;
         using undelegatebw_action = inery::action_wrapper<"undelegatebw"_n, &system_contract::undelegatebw>;
         using buyram_action = inery::action_wrapper<"buyresources"_n, &system_contract::buyresources>;
         using buymembytes_action = inery::action_wrapper<"buymembytes"_n, &system_contract::buymembytes>;
         using sellram_action = inery::action_wrapper<"sellres"_n, &system_contract::sellres>;
         using refund_action = inery::action_wrapper<"refund"_n, &system_contract::refund>;
         using regproducer_action = inery::action_wrapper<"regproducer"_n, &system_contract::regproducer>;
         using unregprod_action = inery::action_wrapper<"unregprod"_n, &system_contract::unregprod>;
         using setmem_action = inery::action_wrapper<"setmem"_n, &system_contract::setmem>;
         using setmemrate_action = inery::action_wrapper<"setmemrate"_n, &system_contract::setmemrate>;
         using makeprod_action = inery::action_wrapper<"makeprod"_n, &system_contract::makeprod>;
         using regproxy_action = inery::action_wrapper<"regproxy"_n, &system_contract::regproxy>;
         using claimrewards_action = inery::action_wrapper<"claimrewards"_n, &system_contract::claimrewards>;
         using rmvproducer_action = inery::action_wrapper<"rmvproducer"_n, &system_contract::rmvproducer>;
         using setpriv_action = inery::action_wrapper<"setpriv"_n, &system_contract::setpriv>;
         using setalimits_action = inery::action_wrapper<"setalimits"_n, &system_contract::setalimits>;
         using setparams_action = inery::action_wrapper<"setparams"_n, &system_contract::setparams>;
         using setinflation_action = inery::action_wrapper<"setinflation"_n, &system_contract::setinflation>;

      private:
         // Implementation details:

         static symbol get_core_symbol( const memmarket& rm ) {
            auto itr = rm.find(memcore_symbol.raw());
            check(itr != rm.end(), "system contract must first be initialized");
            return itr->quote.balance.symbol;
         }

         //defined in inery.system.cpp
         static inery_global_state get_default_parameters();
         static inery_global_state4 get_default_inflation_parameters();
         symbol core_symbol()const;
         void update_mem_supply();

         // defined in delegate_bandwidth.cpp
         void changebw( name from, const name& receiver,
                        const asset& stake_net_quantity, const asset& stake_cpu_quantity, bool transfer );
         void update_voting_power( const name& voter, const asset& total_update );

         // defined in voting.hpp
         void update_elected_producers( const block_timestamp& timestamp );
         void update_votes( const name& voter, const name& proxy, const std::vector<name>& producers, bool voting );
         void propagate_weight_change( const voter_info& voter );
         double update_producer_votepay_share( const producers_table2::const_iterator& prod_itr,
                                               const time_point& ct,
                                               double shares_rate, bool reset_to_zero = false );
         double update_total_votepay_share( const time_point& ct,
                                            double additional_shares_delta = 0.0, double shares_rate_delta = 0.0 );

         template <auto system_contract::*...Ptrs>
         class registration {
            public:
               template <auto system_contract::*P, auto system_contract::*...Ps>
               struct for_each {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, args... );
                     for_each<Ps...>::call( this_contract, std::forward<Args>(args)... );
                  }
               };
               template <auto system_contract::*P>
               struct for_each<P> {
                  template <typename... Args>
                  static constexpr void call( system_contract* this_contract, Args&&... args )
                  {
                     std::invoke( P, this_contract, std::forward<Args>(args)... );
                  }
               };

               template <typename... Args>
               constexpr void operator() ( Args&&... args )
               {
                  for_each<Ptrs...>::call( this_contract, std::forward<Args>(args)... );
               }

               system_contract* this_contract;
         };

         // registration<&system_contract::update_rex_stake> vote_stake_updater{ this };
   };

}
