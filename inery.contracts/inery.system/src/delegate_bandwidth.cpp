#include <inery/datastream.hpp>
#include <inery/inery.hpp>
#include <inery/multi_index.hpp>
#include <inery/privileged.hpp>
#include <inery/serialize.hpp>
#include <inery/transaction.hpp>

#include <inery.system/inery.system.hpp>
#include <inery.token/inery.token.hpp>

namespace inerysystem {

   using inery::asset;
   using inery::const_mem_fun;
   using inery::current_time_point;
   using inery::indexed_by;
   using inery::permission_level;
   using inery::seconds;
   using inery::time_point_sec;
   using inery::token;

   void system_contract::buymembytes( const name& payer, const name& receiver, uint32_t bytes ) {
      auto itr = _memmarket.find(memcore_symbol.raw());
      const int64_t mem_reserve   = itr->base.balance.amount;
      const int64_t ine_reserve   = itr->quote.balance.amount;
      const int64_t cost          = int64_t(exchange_state::get_bancor_input( mem_reserve, ine_reserve, bytes ));
      const int64_t price=cost*10000;
      buyresources( payer, receiver, asset{ price, core_symbol() } );
      delegatebw( payer, receiver, asset{ price, core_symbol() }, asset{ price, core_symbol() }, false );
   }

   void system_contract::buyresources( const name& payer, const name& receiver, const asset& quant ) // quant je INR
   {
      require_auth( payer );
      update_mem_supply();

      check( quant.symbol == core_symbol(), "must buy MEM with core token" );
      check( quant.amount > 0, "must purchase a positive amount" );
      auto INR = quant;
      {
         token::transfer_action transfer_act{ token_account, { {payer, active_permission}, {mem_account, active_permission} } };
         transfer_act.send( payer, mem_account, INR, "buy MEM" );
      }

      int64_t bytes_out;

      const auto& market = _memmarket.get(memcore_symbol.raw(), "MEM market does not exist");
      int64_t memory_balance= market.base.balance.amount;
      _memmarket.modify( market, same_payer, [&]( auto& es ) {
         bytes_out = (es.direct_convert( INR,  mem_symbol ).amount)/10000;
         check( market.base.balance.amount > bytes_out, "error, there is no more memory to buy" );

         });

         int64_t max_mem_size_i=_gstate.max_mem_size;

         double max_mem_size_double=max_mem_size_i;
         double bytes_out_double=bytes_out;
         double memory_balance_double=memory_balance;

      double commit_mem_pom=bytes_out_double/memory_balance_double;
      double commit_mem=commit_mem_pom*max_mem_size_double;

      check( bytes_out > 0, "must reserve a positive amount" );

      _gstate.total_mem_bytes_reserved += uint64_t(commit_mem);
      _gstate.total_mem_stake          += INR.amount;

      user_resources_table  userres( get_self(), receiver.value );
      auto res_itr = userres.find( receiver.value );
      if( res_itr ==  userres.end() ) {
         res_itr = userres.emplace( receiver, [&]( auto& res ) {
               res.owner = receiver;
               res.net_weight = asset( 0, core_symbol() );
               res.cpu_weight = asset( 0, core_symbol() );
               res.mem_bytes = bytes_out;
            });
      } else {
         userres.modify( res_itr, receiver, [&]( auto& res ) {
               res.mem_bytes += bytes_out;
            });
      }
      delegatebw( payer, receiver, asset{ bytes_out*10000/(1024*1024), core_symbol() }, asset{ bytes_out*10000/(1024*1024), core_symbol() }, false );

      auto voter_itr = _voters.find( res_itr->owner.value );
      if( voter_itr == _voters.end() || !has_field( voter_itr->flags1, voter_info::flags1_fields::mem_managed ) ) {
         int64_t mem_bytes, net, cpu;
         get_resource_limits( res_itr->owner, mem_bytes, net, cpu );
         set_resource_limits( res_itr->owner, res_itr->mem_bytes + mem_gift_bytes, net, cpu );
      }
   }

   void system_contract::sellres( const name& account, int64_t bytes ) {
      require_auth( account );
      update_mem_supply();

      check( bytes > 0, "cannot sell negative byte" );
      const auto& prod = _producers.get( account.value );
      user_resources_table  userres( get_self(), account.value );
      auto res_itr = userres.find( account.value );
      const int64_t memory_check= res_itr->memory_return();
      if(prod.active_producer())
      check( memory_check-bytes>(1024*1024), "you can not sell memory, couse you want to sell free resources");
      
      
      check( res_itr != userres.end(), "no resource row" );
      check( res_itr->mem_bytes >= bytes, "insufficient quota" );
      auto pom=res_itr->mem_bytes;

      asset tokens_out;
      auto itr = _memmarket.find(memcore_symbol.raw());
      _memmarket.modify( itr, same_payer, [&]( auto& es ) {
         /// the cast to int64_t of bytes is safe because we certify bytes is <= quota which is limited by prior purchases
         tokens_out = es.direct_convert( asset(bytes, mem_symbol), core_symbol());
         tokens_out= tokens_out*10000;

      });
      check( tokens_out.amount > 1, "token amount received from selling MEM is too low" );

      const auto& market = _memmarket.get(memcore_symbol.raw(), "MEM market does not exist");
      int64_t memory_balance= market.base.balance.amount;
      int64_t max_mem_size_i=_gstate.max_mem_size;
      double max_mem_size_double=max_mem_size_i;
      double bytes_out_double=1024*1024;
      double memory_balance_double=memory_balance;
      double commit_mem_pom=bytes_out_double/memory_balance_double;
      double commit_mem=commit_mem_pom*max_mem_size_double;
      float pom2=(bytes/(1024*1024))*commit_mem;

      _gstate.total_mem_bytes_reserved -= static_cast<decltype(_gstate.total_mem_bytes_reserved)>(pom2); // bytes > 0 is asserted above
      _gstate.total_mem_stake          -= tokens_out.amount;

      //// this shouldn't happen, but just in case it does we should prevent it
      check( _gstate.total_mem_stake >= 0, "error, attempt to unstake more tokens than previously staked" );

      userres.modify( res_itr, account, [&]( auto& res ) {
          res.mem_bytes -= bytes;
      });
      undelegatebw( account, account, asset{ tokens_out.amount, core_symbol() }, asset{ tokens_out.amount, core_symbol() });

      auto voter_itr = _voters.find( res_itr->owner.value );
      if( voter_itr == _voters.end() || !has_field( voter_itr->flags1, voter_info::flags1_fields::mem_managed ) ) {
         int64_t mem_bytes, net, cpu;
         get_resource_limits( res_itr->owner, mem_bytes, net, cpu );
         set_resource_limits( res_itr->owner, res_itr->mem_bytes + mem_gift_bytes, net, cpu );
      }

      {
         token::transfer_action transfer_act{ token_account, { {mem_account, active_permission}, {account, active_permission} } };
         transfer_act.send( mem_account, account, asset(tokens_out), "sell MEM" );
      }
   }
   
   void validate_b1_vesting( int64_t stake ) {
      const int64_t base_time = 1527811200; /// 2018-06-01
      const int64_t max_claimable = 100'000'000'0000ll;
      const int64_t claimable = int64_t(max_claimable * double(current_time_point().sec_since_epoch() - base_time) / (10*seconds_per_year) );

      check( max_claimable - claimable <= stake, "b1 can only claim their tokens over 10 years" );
   }

   void system_contract::changebw( name from, const name& receiver,
                                   const asset& stake_net_delta, const asset& stake_cpu_delta, bool transfer )
   {
      require_auth( from );
      check( stake_net_delta.amount != 0 || stake_cpu_delta.amount != 0, "should stake non-zero amount" );
      check( std::abs( (stake_net_delta + stake_cpu_delta).amount )
             >= std::max( std::abs( stake_net_delta.amount ), std::abs( stake_cpu_delta.amount ) ),
             "net and cpu deltas cannot be opposite signs" );

      name source_stake_from = from;
      if ( transfer ) {
         from = receiver;
      }

      // update stake delegated from "from" to "receiver"
      {
         del_bandwidth_table     del_tbl( get_self(), from.value );
         auto itr = del_tbl.find( receiver.value );
         if( itr == del_tbl.end() ) {
            itr = del_tbl.emplace( from, [&]( auto& dbo ){
                  dbo.from          = from;
                  dbo.to            = receiver;
                  dbo.net_weight    = stake_net_delta;
                  dbo.cpu_weight    = stake_cpu_delta;
               });
         }
         else {
            del_tbl.modify( itr, same_payer, [&]( auto& dbo ){
                  dbo.net_weight    += stake_net_delta;
                  dbo.cpu_weight    += stake_cpu_delta;
               });
         }
         check( 0 <= itr->net_weight.amount, "insufficinet staked net bandwidth" );
         check( 0 <= itr->cpu_weight.amount, "insufficinet staked cpu bandwidth" );
         if ( itr->is_empty() ) {
            del_tbl.erase( itr );
         }
      } // itr can be invalid, should go out of scope

      // update totals of "receiver"
      {
         user_resources_table   totals_tbl( get_self(), receiver.value );
         auto tot_itr = totals_tbl.find( receiver.value );
         if( tot_itr ==  totals_tbl.end() ) {
            tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
                  tot.owner = receiver;
                  tot.net_weight    = stake_net_delta;
                  tot.cpu_weight    = stake_cpu_delta;
               });
         } else {
            totals_tbl.modify( tot_itr, from == receiver ? from : same_payer, [&]( auto& tot ) {
                  tot.net_weight    += stake_net_delta;
                  tot.cpu_weight    += stake_cpu_delta;
               });
         }
         check( 0 <= tot_itr->net_weight.amount, "insufficinet staked total net bandwidth" );
         check( 0 <= tot_itr->cpu_weight.amount, "insufficinet staked total cpu bandwidth" );

         {
            bool mem_managed = false;
            bool net_managed = false;
            bool cpu_managed = false;

            auto voter_itr = _voters.find( receiver.value );
            if( voter_itr != _voters.end() ) {
               mem_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::mem_managed );
               net_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::net_managed );
               cpu_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::cpu_managed );
            }

            if( !(net_managed && cpu_managed) ) {
               int64_t mem_bytes, net, cpu;
               get_resource_limits( receiver, mem_bytes, net, cpu );

               set_resource_limits( receiver,
                                    mem_managed ? mem_bytes : std::max( tot_itr->mem_bytes + mem_gift_bytes, mem_bytes ),
                                    net_managed ? net : tot_itr->net_weight.amount,
                                    cpu_managed ? cpu : tot_itr->cpu_weight.amount );
            }
         }

         if ( tot_itr->is_empty() ) {
            totals_tbl.erase( tot_itr );
         }
      } // tot_itr can be invalid, should go out of scope

      // create refund or update from existing refund
      if ( stake_account != source_stake_from ) { //for inery both transfer and refund make no sense
         refunds_table refunds_tbl( get_self(), from.value );
         auto req = refunds_tbl.find( from.value );

         //create/update/delete refund
         auto net_balance = stake_net_delta;
         auto cpu_balance = stake_cpu_delta;
         bool need_deferred_trx = false;


         // net and cpu are same sign by assertions in delegatebw and undelegatebw
         // redundant assertion also at start of changebw to protect against misuse of changebw
         bool is_undelegating = (net_balance.amount + cpu_balance.amount ) < 0;
         bool is_delegating_to_self = (!transfer && from == receiver);

         if( is_delegating_to_self || is_undelegating ) {
            if ( req != refunds_tbl.end() ) { //need to update refund
               refunds_tbl.modify( req, same_payer, [&]( refund_request& r ) {
                  if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) {
                     r.request_time = current_time_point();
                  }
                  r.net_amount -= net_balance;
                  if ( r.net_amount.amount < 0 ) {
                     net_balance = -r.net_amount;
                     r.net_amount.amount = 0;
                  } else {
                     net_balance.amount = 0;
                  }
                  r.cpu_amount -= cpu_balance;
                  if ( r.cpu_amount.amount < 0 ){
                     cpu_balance = -r.cpu_amount;
                     r.cpu_amount.amount = 0;
                  } else {
                     cpu_balance.amount = 0;
                  }
               });

               check( 0 <= req->net_amount.amount, "negative net refund amount" ); //should never happen
               check( 0 <= req->cpu_amount.amount, "negative cpu refund amount" ); //should never happen

               if ( req->is_empty() ) {
                  refunds_tbl.erase( req );
                  need_deferred_trx = false;
               } else {
                  need_deferred_trx = true;
               }
            } else if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) { //need to create refund
               refunds_tbl.emplace( from, [&]( refund_request& r ) {
                  r.owner = from;
                  if ( net_balance.amount < 0 ) {
                     r.net_amount = -net_balance;
                     net_balance.amount = 0;
                  } else {
                     r.net_amount = asset( 0, core_symbol() );
                  }
                  if ( cpu_balance.amount < 0 ) {
                     r.cpu_amount = -cpu_balance;
                     cpu_balance.amount = 0;
                  } else {
                     r.cpu_amount = asset( 0, core_symbol() );
                  }
                  r.request_time = current_time_point();
               });
               need_deferred_trx = true;
            } // else stake increase requested with no existing row in refunds_tbl -> nothing to do with refunds_tbl
         } /// end if is_delegating_to_self || is_undelegating

         if ( need_deferred_trx ) {
            inery::transaction out;
            out.actions.emplace_back( permission_level{from, active_permission},
                                      get_self(), "refund"_n,
                                      from
            );
            out.delay_sec = refund_delay_sec;
            inery::cancel_deferred( from.value ); // TODO: Remove this line when replacing deferred trxs is fixed
            out.send( from.value, from, true );
         } else {
            inery::cancel_deferred( from.value );
         }

         auto transfer_amount = net_balance + cpu_balance;
         if ( 0 < transfer_amount.amount ) {
            token::transfer_action transfer_act{ token_account, { {source_stake_from, active_permission} } };
            transfer_act.send( source_stake_from, stake_account, asset(transfer_amount), "stake bandwidth" );
         }
      }

      // vote_stake_updater( from );
      update_voting_power( from, stake_net_delta + stake_cpu_delta );
   }

   void system_contract::update_voting_power( const name& voter, const asset& total_update )
   {
      auto voter_itr = _voters.find( voter.value );
      if( voter_itr == _voters.end() ) {
         voter_itr = _voters.emplace( voter, [&]( auto& v ) {
            v.owner  = voter;
            v.staked = total_update.amount;
         });
      } else {
         _voters.modify( voter_itr, same_payer, [&]( auto& v ) {
            v.staked += total_update.amount;
         });
      }

      check( 0 <= voter_itr->staked, "stake for voting cannot be negative" );

      if( voter == "b1"_n ) {
         validate_b1_vesting( voter_itr->staked );
      }

      if( voter_itr->producers.size() || voter_itr->proxy ) {
         update_votes( voter, voter_itr->proxy, voter_itr->producers, false );
      }
   }

   void system_contract::delegatebw( const name& from, const name& receiver,
                                     const asset& stake_net_quantity,
                                     const asset& stake_cpu_quantity, bool transfer )
   {
      asset zero_asset( 0, core_symbol() );
      check( stake_cpu_quantity >= zero_asset, "must stake a positive amount" );
      check( stake_net_quantity >= zero_asset, "must stake a positive amount" );
      check( stake_net_quantity.amount + stake_cpu_quantity.amount > 0, "must stake a positive amount" );
      check( !transfer || from != receiver, "cannot use transfer flag if delegating to self" );

      changebw( from, receiver, stake_net_quantity, stake_cpu_quantity, transfer);
   } // delegatebw

   void system_contract::undelegatebw( const name& from, const name& receiver,
                                       const asset& unstake_net_quantity, const asset& unstake_cpu_quantity )
   {
      asset zero_asset( 0, core_symbol() );
      check( unstake_cpu_quantity >= zero_asset, "must unstake a positive amount" );
      check( unstake_net_quantity >= zero_asset, "must unstake a positive amount" );
      check( unstake_cpu_quantity.amount + unstake_net_quantity.amount > 0, "must unstake a positive amount" );


      changebw( from, receiver, -unstake_net_quantity, -unstake_cpu_quantity, false);
   } // undelegatebw


   void system_contract::refund( const name& owner ) {
      require_auth( owner );

      refunds_table refunds_tbl( get_self(), owner.value );
      auto req = refunds_tbl.find( owner.value );
      check( req != refunds_tbl.end(), "refund request not found" );
      check( req->request_time + seconds(refund_delay_sec) <= current_time_point(),
             "refund is not available yet" );
      token::transfer_action transfer_act{ token_account, { {stake_account, active_permission}, {req->owner, active_permission} } };
      transfer_act.send( stake_account, req->owner, req->net_amount + req->cpu_amount, "unstake" );
      refunds_tbl.erase( req );
   }


} //namespace inerysystem
