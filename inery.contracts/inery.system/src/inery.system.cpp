#include <inery.system/inery.system.hpp>
#include <inery.token/inery.token.hpp>

#include <inery/crypto.hpp>
#include <inery/dispatcher.hpp>

#include <cmath>

namespace inerysystem {

   using inery::current_time_point;
   using inery::token;
   using inery::check;

   double get_continuous_rate(int64_t annual_rate) {
      return std::log1p(double(annual_rate)/double(100*inflation_precision));
   }

   system_contract::system_contract( name s, name code, datastream<const char*> ds )
   :native(s,code,ds),
    _voters(get_self(), get_self().value),
    _producers(get_self(), get_self().value),
    _producers2(get_self(), get_self().value),
    _global(get_self(), get_self().value),
    _global2(get_self(), get_self().value),
    _global3(get_self(), get_self().value),
    _global4(get_self(), get_self().value),
    _memmarket(get_self(), get_self().value)
   {
      _gstate  = _global.exists() ? _global.get() : get_default_parameters();
      _gstate2 = _global2.exists() ? _global2.get() : inery_global_state2{};
      _gstate3 = _global3.exists() ? _global3.get() : inery_global_state3{};
      _gstate4 = _global4.exists() ? _global4.get() : get_default_inflation_parameters();
   }

   inery_global_state system_contract::get_default_parameters() {
      inery_global_state dp;
      get_blockchain_parameters(dp);
      return dp;
   }

   inery_global_state4 system_contract::get_default_inflation_parameters() {
      inery_global_state4 gs4;
      gs4.continuous_rate      = get_continuous_rate(default_annual_rate);
      gs4.inflation_pay_factor = default_inflation_pay_factor;
      gs4.votepay_factor       = default_votepay_factor;
      return gs4;
   }

   symbol system_contract::core_symbol()const {
      const static auto sym = get_core_symbol( _memmarket );
      return sym;
   }

   system_contract::~system_contract() {
      _global.set( _gstate, get_self() );
      _global2.set( _gstate2, get_self() );
      _global3.set( _gstate3, get_self() );
      _global4.set( _gstate4, get_self() );
   }

   void system_contract::setmem( uint64_t max_mem_size ) {
      require_auth( get_self() );

      check( _gstate.max_mem_size < max_mem_size, "mem may only be increased" );
      check( max_mem_size < 1024ll*1024*1024*1024*1024, "mem size is unrealistic" );
      check( max_mem_size > _gstate.total_mem_bytes_reserved, "attempt to set max below reserved" );

      auto delta = int64_t(max_mem_size) - int64_t(_gstate.max_mem_size);
      auto itr = _memmarket.find(memcore_symbol.raw());

      /**
       *  Increase the amount of mem for sale based upon the change in max mem size.
       */
      _memmarket.modify( itr, same_payer, [&]( auto& m ) {
         m.base.balance.amount += delta;
      });

      _gstate.max_mem_size = max_mem_size;

   }

   void system_contract::update_mem_supply() {
      auto cbt = inery::current_block_time();

      if( cbt <= _gstate2.last_mem_increase ) return;

      auto itr = _memmarket.find(memcore_symbol.raw());
      auto new_ram = (cbt.slot - _gstate2.last_mem_increase.slot)*_gstate2.new_mem_per_block;
      _gstate.max_mem_size += new_ram;

      /**
       *  Increase the amount of mem for sale based upon the change in max mem size.
       */
      _memmarket.modify( itr, same_payer, [&]( auto& m ) {
         m.base.balance.amount += new_ram;
      });
      _gstate2.last_mem_increase = cbt;
   }

   void system_contract::setmemrate( uint16_t bytes_per_block ) {
      require_auth( get_self() );

      update_mem_supply();
      _gstate2.new_mem_per_block = bytes_per_block;
   }

   void system_contract::setparams( const inery::blockchain_parameters& params ) {
      require_auth( get_self() );
      (inery::blockchain_parameters&)(_gstate) = params;
      check( 3 <= _gstate.max_authority_depth, "max_authority_depth should be at least 3" );
      set_blockchain_parameters( params );
   }

   void system_contract::setpriv( const name& account, uint8_t ispriv ) {
      require_auth( get_self() );
      set_privileged( account, ispriv );
   }

   void system_contract::setalimits( const name& account, int64_t mem, int64_t net, int64_t cpu ) {
      require_auth( get_self() );

      user_resources_table userres( get_self(), account.value );
      auto ritr = userres.find( account.value );
      check( ritr == userres.end(), "only supports unlimited accounts" );

      auto vitr = _voters.find( account.value );
      if( vitr != _voters.end() ) {
         bool mem_managed = has_field( vitr->flags1, voter_info::flags1_fields::mem_managed );
         bool net_managed = has_field( vitr->flags1, voter_info::flags1_fields::net_managed );
         bool cpu_managed = has_field( vitr->flags1, voter_info::flags1_fields::cpu_managed );
         check( !(mem_managed || net_managed || cpu_managed), "cannot use setalimits on an account with managed resources" );
      }

      set_resource_limits( account, mem, net, cpu );
   }

   void system_contract::setacctram( const name& account, const std::optional<int64_t>& mem_bytes ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t mem = 0;

      if( !mem_bytes ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::mem_managed ),
                "MEM of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         mem = mem_gift_bytes;
         if( ritr != userres.end() ) {
            mem += ritr->mem_bytes;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::mem_managed, false );
         });
      } else {
         check( *mem_bytes >= 0, "not allowed to set MEM limit to unlimited" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::mem_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::mem_managed, true );
            });
         }

         mem = *mem_bytes;
      }

      set_resource_limits( account, mem, current_net, current_cpu );
   }

   void system_contract::setacctnet( const name& account, const std::optional<int64_t>& net_weight ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t net = 0;

      if( !net_weight ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::net_managed ),
                "Network bandwidth of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         if( ritr != userres.end() ) {
            net = ritr->net_weight.amount;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, false );
         });
      } else {
         check( *net_weight >= -1, "invalid value for net_weight" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::net_managed, true );
            });
         }

         net = *net_weight;
      }

      set_resource_limits( account, current_ram, net, current_cpu );
   }

   void system_contract::setacctcpu( const name& account, const std::optional<int64_t>& cpu_weight ) {
      require_auth( get_self() );

      int64_t current_ram, current_net, current_cpu;
      get_resource_limits( account, current_ram, current_net, current_cpu );

      int64_t cpu = 0;

      if( !cpu_weight ) {
         auto vitr = _voters.find( account.value );
         check( vitr != _voters.end() && has_field( vitr->flags1, voter_info::flags1_fields::cpu_managed ),
                "CPU bandwidth of account is already unmanaged" );

         user_resources_table userres( get_self(), account.value );
         auto ritr = userres.find( account.value );

         if( ritr != userres.end() ) {
            cpu = ritr->cpu_weight.amount;
         }

         _voters.modify( vitr, same_payer, [&]( auto& v ) {
            v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, false );
         });
      } else {
         check( *cpu_weight >= -1, "invalid value for cpu_weight" );

         auto vitr = _voters.find( account.value );
         if ( vitr != _voters.end() ) {
            _voters.modify( vitr, same_payer, [&]( auto& v ) {
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, true );
            });
         } else {
            _voters.emplace( account, [&]( auto& v ) {
               v.owner  = account;
               v.flags1 = set_field( v.flags1, voter_info::flags1_fields::cpu_managed, true );
            });
         }

         cpu = *cpu_weight;
      }

      set_resource_limits( account, current_ram, current_net, cpu );
   }

   void system_contract::activate( const inery::checksum256& feature_digest ) {
      require_auth( get_self() );
      preactivate_feature( feature_digest );
   }

   void system_contract::rmvproducer( const name& producer ) {
      require_auth( get_self() );
      auto prod = _producers.find( producer.value );
      check( prod != _producers.end(), "producer not found" );
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.deactivate();
         });
   }

   void system_contract::setinflation( int64_t annual_rate, int64_t inflation_pay_factor, int64_t votepay_factor ) {
      require_auth(get_self());
      check(annual_rate >= 0, "annual_rate can't be negative");
      if ( inflation_pay_factor < pay_factor_precision ) {
         check( false, "inflation_pay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      if ( votepay_factor < pay_factor_precision ) {
         check( false, "votepay_factor must not be less than " + std::to_string(pay_factor_precision) );
      }
      _gstate4.continuous_rate      = get_continuous_rate(annual_rate);
      _gstate4.inflation_pay_factor = inflation_pay_factor;
      _gstate4.votepay_factor       = votepay_factor;
      _global4.set( _gstate4, get_self() );
   }


   void native::newaccount( const name&       creator,
                            const name&       newact,
                            ignore<authority> owner,
                            ignore<authority> active ) {

      if( creator != get_self() ) {
         uint64_t tmp = newact.value >> 4;
         bool has_dot = false;

         for( uint32_t i = 0; i < 12; ++i ) {
           has_dot |= !(tmp & 0x1f);
           tmp >>= 5;
         }
      }

      user_resources_table  userres( get_self(), newact.value );

      userres.emplace( newact, [&]( auto& res ) {
        res.owner = newact;
        res.net_weight = asset( 0, system_contract::get_core_symbol() );
        res.cpu_weight = asset( 0, system_contract::get_core_symbol() );
      });

      set_resource_limits( newact, 0, 0, 0 );
   }

   void native::setabi( const name& acnt, const std::vector<char>& abi ) {
      inery::multi_index< "abihash"_n, abi_hash >  table(get_self(), get_self().value);
      auto itr = table.find( acnt.value );
      if( itr == table.end() ) {
         table.emplace( acnt, [&]( auto& row ) {
            row.owner = acnt;
            row.hash = inery::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      } else {
         table.modify( itr, same_payer, [&]( auto& row ) {
            row.hash = inery::sha256(const_cast<char*>(abi.data()), abi.size());
         });
      }
   }
   void system_contract::init( unsigned_int version, const symbol& core ) {
      require_auth( get_self() );
      check( version.value == 0, "unsupported version for init action" );
      auto itr = _memmarket.find(memcore_symbol.raw());
      check( itr == _memmarket.end(), "system contract has already been initialized" );

      auto system_token_supply   = inery::token::get_supply(token_account, core.code() );
      check( system_token_supply.symbol == core, "specified core symbol does not exist (precision mismatch)" );

      check( system_token_supply.amount > 0, "system token supply must be greater than 0" );
      _memmarket.emplace( get_self(), [&]( auto& m ) {
         m.supply.amount = 100000000000000ll;
         m.supply.symbol = memcore_symbol;

         m.base.balance.amount = int64_t(353.5*1024*1024*1024*1024);

         m.base.balance.symbol = mem_symbol;

         m.quote.balance.amount = system_token_supply.amount / 1000;
         m.quote.balance.symbol = core;
      });
   }
   asset direct_convert( const asset& from, const symbol& to )
   {
      const auto& sell_symbol  = from.symbol;
      const auto& base_symbol  = base.balance.symbol;
      const auto& quote_symbol = quote.balance.symbol;
      check( sell_symbol != to, "cannot convert to the same symbol" );

      asset out( 0, to );

      if ( sell_symbol == base_symbol && to == quote_symbol ) { 
         out.amount = get_bancor_input( base.balance.amount, quote.balance.amount, from.amount );
         base.balance  += from;
       
         quote.balance -= out*10000;
    
      } else if ( sell_symbol == quote_symbol && to == base_symbol ) { 
         out.amount = get_bancor_output( quote.balance.amount, base.balance.amount, from.amount );
         quote.balance += from;
         base.balance  -= out/10000;
                 

      } else {
         check( false, "invalid conversion" );
      }
      return out;
   }

   int64_t get_bancor_output( int64_t inp_reserve,
                              int64_t out_reserve,
                              int64_t inp ) 
   {
      const double INR = inp_reserve;
      const double mem = out_reserve;
      const double tokens = inp;

     
      int64_t out=int64_t(tokens*1024*1024);
      if ( out < 0 ) out = 0;

      return out;
   }

   int64_t get_bancor_input( int64_t out_reserve, 
                             int64_t inp_reserve,
                             int64_t out ) 
   {
      const double mem = out_reserve; 
      const double INR = inp_reserve; 
      const int64_t tokens = out;
      int64_t cost_in_INR = tokens/(1024*1024);
     
      if ( cost_in_INR < 0 ) cost_in_INR = 0;

      return cost_in_INR; 
   }

} /// inery.system
 /// namespace inerysystem