#pragma once
//#include <eosiolib/action.hpp>
//using std::string;

class player_control : public control_base {
private:
    player_table players;

    account_name self;
    rule_controller<rivnprice, rivnprice_table> rivnprice_controller;
    saleslog_control &saleslog_controller;
    admin_control &admin_controller;
    variable_control &variable_controller;

    struct st_transfer {
        account_name from;
        account_name to;
        asset        quantity;
        std::string  memo;
    };
    
public:
    // constructor
    //-------------------------------------------------------------------------
    player_control(account_name _self,
                   saleslog_control &_saleslog_controller,
                   admin_control &_admin_controller,
                   variable_control &_variable_controller)
        : self(_self)
        , players(_self, _self)
        , rivnprice_controller(_self, N(ivnprice))
        , saleslog_controller(_saleslog_controller)
        , admin_controller(_admin_controller)
        , variable_controller(_variable_controller) {
    }

    // internal apis
    //-------------------------------------------------------------------------
    player_table& get_players() {
        return players;
    }

    player_table::const_iterator get_player(name player_name) {
        return players.find(player_name);
    }
    
    bool is_empty_player(player_table::const_iterator player) {
        return player == players.end();
    }

    void increase_powder(player_table::const_iterator player, uint32_t powder) {
        // modify powder
        players.modify(player, self, [&](auto& target) {
            target.powder += powder;
        });
    }

    void decrease_powder(player_table::const_iterator player, uint32_t powder) {
        assert_true(player->powder >= powder, "not enough powder");

        // modify powder
        players.modify(player, self, [&](auto& target) {
            target.powder -= powder;
        });
    }

    template<typename T>
    void eosiotoken_transfer(uint64_t sender, uint64_t receiver, T func) {
        auto transfer_data = eosio::unpack_action_data<st_transfer>();
        eosio_assert(transfer_data.quantity.symbol == S(4, EOS), "only accepts EOS for deposits");
        eosio_assert(transfer_data.quantity.is_valid(), "Invalid token transfer");
        eosio_assert(transfer_data.quantity.amount > 0, "Quantity must be positive");

        if (transfer_data.from == self) {
            auto to = to_name(transfer_data.to);
            auto to_player = players.find(to);

            // stockholder could be stockholder. so separate withdraw and dividend by message
            if (admin_controller.is_stock_holder(to) && 
                transfer_data.memo.compare(admin_controller.dividend_message) == 0) {
                // stock share
                admin_controller.add_dividend(transfer_data.quantity);
            } else if (to_player != players.cend()) {
                // player withdraw
                return;
            } else {
                // expense
                admin_controller.add_expenses(transfer_data.quantity, to, transfer_data.memo);
            }
        } else if (transfer_data.to == self) {
            auto from = to_name(transfer_data.from);
            auto from_player = players.find(from);
            
            if (transfer_data.memo == "investment") {
                admin_controller.add_investment(transfer_data.quantity);
            } else if (from_player == players.end()) {
                // system account could transfer eos to contract
                // eg) unstake, sellram, etc
                // add to the revenue for these.
                if (is_system_account(transfer_data.from)) {
                    admin_controller.add_revenue(transfer_data.quantity, rv_system);
                } else {
                    assert_true(false, "sign up first!");
                }
            } else {
                // player's deposit action
                transfer_action res;
                size_t center = transfer_data.memo.find(':');
                res.from = from;
                res.action = transfer_data.memo.substr(0, center);
                res.param = transfer_data.memo.substr(center + 1);
                res.quantity = transfer_data.quantity;       
                func(res);
            }
        }
    }

    bool is_system_account(account_name name) {
        if (name == N(eosio.bpay) || 
            name == N(eosio.msig) ||
            name == N(eosio.names) ||
            name == N(eosio.ram) ||
            name == N(eosio.ramfee) ||
            name == N(eosio.saving) ||
            name == N(eosio.stake) || 
            name == N(eosio.token) || 
            name == N(eosio.vpay) ) {
            return true;
        }
        return false;
    }

    void new_player(name from) {
        auto& variables = variable_controller.get_rvariable_rule();
        auto& rules = variables.get_table();
        auto rule = rules.find(vt_init_powder);
        eosio_assert(rule != rules.end(), "can not found powder rule" );

        auto itr = players.emplace(self, [&](auto& target) {
            target.owner = from;
            target.powder = rule->value;
            target.current_stage = 1;
        });

        admin_controller.record_new_player();
    }

    // actions
    //-------------------------------------------------------------------------
    void signup(name from) {
        require_auth(from);
        auto iter = players.find(from);
        eosio_assert(iter == players.end(), "already signed up" );
        new_player(from);
    }

    void itemivnup(name from, const asset &quantity) {
        require_auth(from);
        auto player = players.find(from);
        assert_true(player != players.end(), "could not find player");


        uint8_t ts = player->item_ivn_up + 1;
        assert_true(ts <= kv_max_item_inventory_up, "can not exceed max size");

        auto &ivnprice_table = rivnprice_controller.get_table();
        auto price = ivnprice_table.find((uint64_t)ts);
        assert_true(price != ivnprice_table.end(), "no price rule");
        assert_true(quantity.amount == price->price.amount, "ivn price does not match");

        name seller;
        seller.value = self;

        buylog blog;
        blog.seller = seller;
        blog.dt = time_util::getnow();
        blog.type = ct_item_iventory_up;
        blog.pid = ts;
        blog.code = 0;
        blog.dna = 0;
        blog.level = 0;
        blog.exp = 0;
        blog.price = price->price;
        saleslog_controller.add_buylog(blog, from);

        // modify inventory size
        players.modify(player, self, [&](auto& target) {
            target.item_ivn_up = ts;
        });
    }

    void mativnup(name from, const asset &quantity) {
        require_auth(from);
        auto player = players.find(from);
        assert_true(player != players.end(), "could not find player");

        uint8_t ts = player->mat_ivn_up + 1;
        assert_true(ts <= kv_max_material_inventory_up, "can not exceed max size");

        auto &ivnprice_table = rivnprice_controller.get_table();
        auto price = ivnprice_table.find((uint64_t)ts);
        assert_true(price != ivnprice_table.end(), "no price rule");
        assert_true(quantity.amount == price->price.amount, "ivn price does not match");

        name seller;
        seller.value = self;

        buylog blog;
        blog.seller = seller;
        blog.dt = time_util::getnow();
        blog.type = ct_mat_iventory_up;
        blog.pid = ts;
        blog.code = 0;
        blog.dna = 0;
        blog.level = 0;
        blog.exp = 0;
        blog.price = price->price;
        saleslog_controller.add_buylog(blog, from);

        // modify inventory size
        players.modify(player, self, [&](auto& target) {
            target.mat_ivn_up = ts;
        });
    }

    rule_controller<rivnprice, rivnprice_table>& get_inventory_price_rule() {
        return  rivnprice_controller;
    }
};
