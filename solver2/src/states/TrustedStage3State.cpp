#include <states/TrustedStage3State.h>
#include <SolverContext.h>

#pragma warning(push)
#pragma warning(disable: 4267 4244 4100 4245)
#include <Solver/Generals.hpp>
#pragma warning(pop)

#include <math.h>

namespace slv2
{
    void TrustedStage3State::on(SolverContext & context)
    {
        DefaultStateBehavior::on(context);

        memset(&stage, 0, sizeof(stage));
        stage.sender = (uint8_t) context.own_conf_number();
        const auto ptr = context.stage2(stage.sender);
        if (ptr == nullptr) {
          if (Consensus::Log) {
            LOG_WARN(name() << ": stage one result not found");
          }
        }
        // process already received stage-2, possible to go further to stage-3
        for(const auto& st : context.stage2_data()) {
            if(Result::Finish == onStage2(context, st)) {
                context.complete_stage3();
                return;
            }
        }

        SolverContext *pctx = &context;
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": start track timeout " << Consensus::T_stage_request
            << " ms of stages-2 received");
        }
        timeout_request_stage.start(
          context.scheduler(),
          Consensus::T_stage_request,
          // timeout handler:
          [pctx, this]() {
          if (Consensus::Log) {
            LOG_NOTICE(name() << ": timeout for stages-2 is expired, make requests");
          }
          request_stages(*pctx);
          // start subsequent track timeout for "wide" request
          if (Consensus::Log) {
            LOG_NOTICE(name() << ": start subsequent track timeout " << Consensus::T_stage_request
              << " ms to request neighbors about stages-2");
          }
          timeout_request_neighbors.start(
            pctx->scheduler(),
            Consensus::T_stage_request,
            // timeout handler:
            [pctx, this]() {
            if (Consensus::Log) {
              LOG_NOTICE(name() << ": timeout for transition is expired, make requests to neighbors");
            }
            request_stages_neighbors(*pctx);
          },
            true /*replace if exists*/);
        },
          true /*replace if exists*/);
    }

    void TrustedStage3State::off(SolverContext & /*context*/)
    {
      if (timeout_request_stage.cancel()) {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": cancel track timeout of stages-2");
        }
      }
      if (timeout_request_neighbors.cancel()) {
        if (Consensus::Log) {
          LOG_NOTICE(name() << ": cancel track timeout to request neighbors about stages-2");
        }
      }
    }

    // requests stages from corresponded nodes
    void TrustedStage3State::request_stages(SolverContext& context)
    {
      uint8_t cnt = (uint8_t)context.cnt_trusted();
      for (uint8_t i = 0; i < cnt; ++i) {
        if (context.stage2(i) == nullptr) {
          context.request_stage2(i, i);
        }
      }
    }

    // requests stages from any available neighbor nodes
    void TrustedStage3State::request_stages_neighbors(SolverContext& context)
    {
      const auto& stage2_data = context.stage2_data();
      uint8_t cnt = (uint8_t)context.cnt_trusted();
      for (uint8_t i = 0; i < cnt; ++i) {
        if (context.stage2(i) == nullptr) {
          for (const auto& d : stage2_data) {
            if (d.sender != context.own_conf_number()) {
              context.request_stage2(d.sender, i);
            }
          }
        }
      }
    }

    Result TrustedStage3State::onStage2(SolverContext & context, const Credits::StageTwo & st)
    {
        LOG_DEBUG(__func__);
        const auto ptr = context.stage2((uint8_t)context.own_conf_number());
        if(ptr != nullptr && context.enough_stage2()) {
            LOG_NOTICE(name() << ": enough stage-2 received");
            const auto cnt = context.cnt_trusted();
            constexpr size_t sig_len = sizeof(st.signatures[0].val);
            for(int i = 0; i < cnt; i++) {
                // check amount of trusted node's signatures nonconformity
                if(memcmp(ptr->signatures[i].val, st.signatures[i].val, sig_len) != 0) {
                    LOG_WARN(name() << ": [" << (int)st.sender << "] marked as untrusted");
                    context.mark_untrusted(st.sender);
                }
            }

            trusted_election(context);
            if(pool_solution_analysis(context)) {
                stage.writer = context.generals().takeUrgentDecision(
                    context.cnt_trusted(), context.blockchain().getHashBySequence(static_cast<uint32_t>(context.round()) - 1));
                LOG_NOTICE(name() << ": consensus -> [" << (int)stage.writer << "]");
            }
            else {
                LOG_WARN(name() << ": consensus is not achieved");
            }
            // all trusted nodes must send stage3 data
            LOG_NOTICE(name() << ": --> stage-3 [" << (int) stage.sender << "]");
            context.add_stage3(stage);//, stage.writer != stage.sender);
            context.next_trusted_candidates(next_round_trust);
            //if(stage.writer == stage.sender) {
            //    // we are selected to write & send block
            //    LOG_NOTICE(name() << ": spawn next round");
            //    context.spawn_next_round();
            //}
            return Result::Finish;
        }
        LOG_DEBUG(name() << ": ignore prepare block");
        return Result::Ignore;
    }

    bool TrustedStage3State::pool_solution_analysis(SolverContext& context)
    {
        struct HashWeight
        {
            uint8_t hash[32];
            uint8_t weight { 0 };
        };

        std::vector <HashWeight> hWeight;
        HashWeight everyHashWeight;
        //creating hash frequency table
        for(const auto& it : context.stage1_data()) {
            if(hWeight.size() == 0) {
                memcpy(everyHashWeight.hash, it.hash.val, 32);
                everyHashWeight.weight = 1;
                hWeight.push_back(everyHashWeight);
            }
            else {
                bool found = false;
                for(auto& itt : hWeight) {
                    if(memcmp(itt.hash, it.hash.val, 32) == 0) {
                        ++(itt.weight);
                        found = true;
                        break;
                    }
                }
                if(!found) {
                    memcpy(everyHashWeight.hash, it.hash.val, 32);
                    everyHashWeight.weight = 1;
                    hWeight.push_back(everyHashWeight);
                }
            }
        }
        size_t maxWeight = 0;
        Credits::Hash_ mostFrequentHash;
        memset(&mostFrequentHash, 0, sizeof(mostFrequentHash));

        ////searching for most frequent hash 
        for(auto& it : hWeight) {
            if(it.weight > maxWeight) {
                maxWeight = it.weight;
                memcpy(mostFrequentHash.val, it.hash, 32);
            }
        }
        std::cout << "================================= SUMMARY ======================================= " << std::endl;
        uint8_t liarNumber = 0;
        /* std::cout <<  "Most Frequent hash: " << byteStreamToHex((const char*)mostFrequentHash.val, 32) << std::endl;*/
        for(const auto& it : context.stage1_data()) {

            if(memcmp(it.hash.val, mostFrequentHash.val, 32) == 0) {
                std::cout << "\t[" << (int) it.sender << "] is not liar " << byteStreamToHex((const char*) it.hash.val, 32)
                    << std::endl;
            }
            else {
                ++liarNumber;
                std::cout << "\t[" << (int) it.sender << "] IS LIAR " << byteStreamToHex((const char*) it.hash.val, 32)
                    << std::endl;
            }
        }

        std::cout << "\tLiars amount: " << (int) liarNumber << std::endl;
        std::cout << "================================================================================= " << std::endl;
        if(liarNumber > context.cnt_trusted() / 2) {
            return false;
        }
        else {
            return true;
        }
    }

    void TrustedStage3State::trusted_election(SolverContext& context)
    {
        if(!next_round_trust.empty()) {
            next_round_trust.clear();
        }
        std::array<uint8_t,Consensus::MaxTrustedNodes> trustedMask;
        trustedMask.fill(0);
        std::map <PublicKey, uint8_t> candidatesElection;
        const uint8_t cnt_trusted = std::min((uint8_t) context.cnt_trusted(), (uint8_t) Consensus::MaxTrustedNodes);
        uint8_t cr = cnt_trusted / 2;
        std::vector <PublicKey> aboveThreshold;
        std::vector <PublicKey> belowThreshold;

        LOG_NOTICE(name() << ": number of generals / 2 = " << (int) cr);

        for(uint8_t i = 0; i < cnt_trusted; i++) {
            trustedMask[i] = (context.untrusted_value(i) <= cr);
            if(trustedMask[i]) {
                const auto& stage_i = *(context.stage1_data().cbegin() + i);
                uint8_t candidates_amount = stage_i.candidatesAmount;
                for(uint8_t j = 0; j < candidates_amount; j++) {
                    //std::cout << (int) i << "." << (int) j << " " << byteStreamToHex(stageOneStorage.at(i).candiates[j].str, 32) << std::endl;
                    if(candidatesElection.count(stage_i.candiates[j]) > 0) {
                        candidatesElection.at(stage_i.candiates[j]) += 1;
                    }
                    else {
                        candidatesElection.emplace(stage_i.candiates[j], (uint8_t) 1);
                    }
                }
            }
        }

        LOG_NOTICE(name() << ": election table ready");
        unsigned int max_conf = int(4. + 1.85 * log(candidatesElection.size() / 4.));
        if (candidatesElection.size() < 4) {
            max_conf = (unsigned int) candidatesElection.size();
            LOG_WARN(name() << ": too few TRUSTED NODES, but we continue at the minimum ...");
        }


        for(auto& it : candidatesElection) {
            //std::cout << byteStreamToHex(it.first.str, 32) << " - " << (int) it.second << std::endl;
            if(it.second > cr) {
                aboveThreshold.push_back(it.first);
            }
            else {
                belowThreshold.push_back(it.first);
            }
        }

        LOG_NOTICE(name() << ": candidates divided: above = " << aboveThreshold.size() << ", below = " << belowThreshold.size());
        LOG_DEBUG("======================================================");
        for(int i = 0; i < aboveThreshold.size(); i++) {
            LOG_DEBUG(i << ". " << byteStreamToHex(aboveThreshold.at(i).str, 32)
                << " - " << (int) candidatesElection.at(aboveThreshold.at(i)));
        }
        LOG_DEBUG("------------------------------------------------------");
        for(int i = 0; i < belowThreshold.size(); i++) {
            LOG_DEBUG(i << ". " << byteStreamToHex(belowThreshold.at(i).str, 32)
                << " - " << (int) candidatesElection.at(belowThreshold.at(i)));
        }
        LOG_NOTICE(name() << ": final list of next round trusted:");

        if(aboveThreshold.size() >= max_conf) { // Consensus::MinTrustedNodes) {
            for(unsigned int i = 0; i < max_conf; ++i) {
                next_round_trust.push_back(aboveThreshold.at(i));
                LOG_NOTICE(byteStreamToHex(next_round_trust.back().str, 32));
            }
        }
        else {
            if(belowThreshold.size() >= max_conf - aboveThreshold.size()) {
                for(int i = 0; i < aboveThreshold.size(); i++) {
                    next_round_trust.push_back(aboveThreshold.at(i));
                    LOG_NOTICE(byteStreamToHex(next_round_trust.back().str, 32));
                }
                for(int i = 0; i < max_conf - next_round_trust.size(); i++) {
                    next_round_trust.push_back(belowThreshold.at(i));
                    LOG_NOTICE(byteStreamToHex(next_round_trust.back().str, 32));
                }
            }
            else {
                LOG_WARN(name() << ": cannot create list of trusted, too few candidates.");
            }
        }
        LOG_NOTICE(name() << ": end of trusted election");
    }

} // slv2
