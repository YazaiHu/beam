#pragma once

#include "core/common.h"
#include "core/ecc_native.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127 )
#endif

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/functor_row.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "core/serialization_adapters.h"

namespace beam
{
    using Uuid = std::array<uint8_t, 16>;

    struct PrintableAmount
    {
        explicit PrintableAmount(const Amount& amount) : m_value{amount}
        {}
        const Amount& m_value;
    };

    std::ostream& operator<<(std::ostream& os, const PrintableAmount& amount);
    std::ostream& operator<<(std::ostream& os, const Uuid& uuid);

    struct Coin;
    using UuidPtr = std::shared_ptr<Uuid>;
    using TransactionPtr = std::shared_ptr<Transaction>;
    std::pair<ECC::Scalar::Native, ECC::Scalar::Native> split_key(const ECC::Scalar::Native& key, uint64_t index);

    namespace wallet
    {
        namespace msm = boost::msm;
        namespace msmf = boost::msm::front;
        namespace mpl = boost::mpl;

        template <typename Derived>
        class FSMHelper 
        {
        public:
            void start()
            {
                static_cast<Derived*>(this)->m_fsm.start();
            }

            template<typename Event>
            bool process_event(const Event& event)
            {
                auto* d = static_cast<Derived*>(this);
                auto res = d->m_fsm.process_event(event) == msm::back::HANDLED_TRUE;
                d->update_history();
                return res;
            }

            template<class Archive>
            void serialize(Archive & ar, const unsigned int)
            {
                static_cast<Derived*>(this)->m_fsm.serialize(ar, 0);
            }
            // for test only
            const int* current_state() const
            {
                return static_cast<const Derived*>(this)->m_fsm.current_state();
            }
        };

        // messages
        struct InviteReceiver
        {
            Uuid m_txId;
            ECC::Amount m_amount;
            Height m_height;
            ECC::Hash::Value m_message;
            ECC::Point m_publicSenderBlindingExcess;
            ECC::Point m_publicSenderNonce;
            std::vector<Input::Ptr> m_inputs;
            std::vector<Output::Ptr> m_outputs;

            SERIALIZE(m_txId
                    , m_amount
                    , m_height
                    , m_message
                    , m_publicSenderBlindingExcess
                    , m_publicSenderNonce
                    , m_inputs
                    , m_outputs);
        };

        struct ConfirmTransaction
        {
            Uuid m_txId;
            ECC::Scalar m_senderSignature;

            SERIALIZE(m_txId, m_senderSignature);
        };

        struct ConfirmInvitation
        {
            Uuid m_txId;
            ECC::Point m_publicReceiverBlindingExcess;
            ECC::Point m_publicReceiverNonce;
            ECC::Scalar m_receiverSignature;

            SERIALIZE(m_txId
                    , m_publicReceiverBlindingExcess
                    , m_publicReceiverNonce
                    , m_receiverSignature);
        };

        struct TxRegistered
        {
            Uuid m_txId;
            bool m_value;
            SERIALIZE(m_txId, m_value);
        };

        struct TxFailed
        {
            Uuid m_txId;
            SERIALIZE(m_txId);
        };

        struct IWalletGateway
        {
            virtual ~IWalletGateway() {}
            virtual void on_tx_completed(const Uuid& txId) = 0;
            virtual void send_tx_failed(const Uuid& txId) = 0;
        };

        namespace sender
        {
            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_invitation(const InviteReceiver&) = 0;
                virtual void send_tx_confirmation(const ConfirmTransaction&) = 0;
            };
        }

        namespace receiver
        {
            struct IGateway : virtual IWalletGateway
            {
                virtual void send_tx_confirmation(const ConfirmInvitation&) = 0;
                virtual void register_tx(const Uuid&, Transaction::Ptr) = 0;
                virtual void send_tx_registered(UuidPtr&&) = 0;
            };
        }
    }
}