#include "waitingstate.hpp"
#include <consensus.hpp>
#include <solvercontext.hpp>
#include <sstream>

namespace cs {

void WaitingState::on(SolverContext &context) {
  // TODO:: calculate my queue number starting from writing node:
  const auto ptr = context.stage3(context.own_conf_number());
  writingQueueNumber_ = ptr->realTrustedMask.at(ptr->sender);  
  if (writingQueueNumber_ == InvalidConfidantIndex) {
    return;
  }

  cs::BlockSignatures blockSignatures;
  blockSignatures.reserve(context.stage3_data().size());
  for (auto& it : context.stage3_data()) {
    blockSignatures.emplace_back(it.sender, it.blockSignature);
  }

  //TODO: The pool should be send to blockchain here <===
  csmeta(csdebug) << "Signatures to add: " << blockSignatures.size();
  if (!context.addSignaturesToLastBlock(std::move(blockSignatures))) {
    csmeta(cserror) << "Signatures added to new block failed";
    return;
  }

  csmeta(csdebug) << "Signatures added to new block successfully";

  std::ostringstream os;
  os << prefix_ << "-" << static_cast<size_t>(writingQueueNumber_);
  myName_ = os.str();

  const size_t invalids_count = static_cast<size_t>(std::count(ptr->realTrustedMask.cbegin(), ptr->realTrustedMask.cend(), InvalidConfidantIndex));
  const size_t cnt_active = ptr->realTrustedMask.size() - invalids_count;
  csdebug() << name() << ": my order " << static_cast<int>(ptr->sender)
            << ", trusted " << context.cnt_trusted()
            << " (good " << cnt_active << ")"
            << ", writer " << static_cast<int>(ptr->writer);

  if (writingQueueNumber_ == 0) {
    csdebug() << name() << ": becoming WRITER";
    context.request_role(Role::Writer);
    return;
  }

  // TODO: value = (Consensus::PostConsensusTimeout * "own number in "writing" queue")
  const uint32_t value = sendRoundTableDelayMs_ * writingQueueNumber_;

  if (Consensus::Log) {
    csdebug() << name() << ": start wait " << value / 1000 << " sec until new round";
  }

  SolverContext *pctx = &context;
  roundTimeout_.start(context.scheduler(), value,
                      [pctx, this]() {
                        if (Consensus::Log) {
                          csdebug() << name() << ": time to wait new round is expired";
                        }
                        activate_new_round(*pctx);
                      },
                      true /*replace existing*/);
}

void WaitingState::off(SolverContext & /*context*/) {
  myName_ = prefix_;
  if (roundTimeout_.cancel()) {
    if (Consensus::Log) {
      csdebug() << name() << ": cancel wait new round";
    }
  }
}

void WaitingState::activate_new_round(SolverContext &context) {
  csdebug() << name() << ": activating new round ";
  const auto ptr = context.stage3(context.own_conf_number());
  if (ptr == nullptr) {
    cserror() << name() << ": cannot access own stage data, didnt you forget to cancel this call?";
    return;
  }

  const uint8_t writer = ptr->writer + writingQueueNumber_;
  context.request_round_info((writer - 1) % context.cnt_trusted(),   // previous "writer"
                             (writer + 1) % context.cnt_trusted());  // next "writer"
}

}  // namespace cs
