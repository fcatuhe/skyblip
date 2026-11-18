// A trusted browser activates a unit with no press, and the press an untrusted one needs trusts it.
#include <string>

#include "core/comms/config.h"
#include "core/comms/link_registry.h"
#include "core/util/hex.h"
#include "doctest/doctest.h"
#include "test/support/activation_vectors.h"
#include "test/support/product_rig.h"

using namespace skyblip;

namespace {

// INFO: fc 16nov26 what Chrome negotiates, wide enough for the registration frame
constexpr uint16_t kChromePayloadBytes = 495;

const crypto::Ed25519::Seed kBrowserSeed{0x42, 51};
const identity::Nonce kBrowserNonce{6, 6, 6, 6};

std::string hex_text(ConstByteSpan bytes) {
    std::string out(2 * bytes.size(), '?');
    hex::write(bytes, out.data());
    return out;
}

std::string refusal(const char* word) {
    return std::string("{\"command\":\"acknowledgement\",\"acknowledge\":false,\"reason\":\"") +
           word + "\"}";
}

struct Bench {
    Rig rig{platform::host::Platform::kFullyFitted, std::nullopt};
    ActivationVectors vectors;
    uint32_t now_ms{0};

    Bench() {
        rig.platform.dfu().running_version_valid = true;
        rig.platform.dfu().running_image_version = ports::ImageVersion{0, 3, 0, 1234};
        rig.platform.link().declare_payload_bytes(kChromePayloadBytes);
        REQUIRE(rig.setup() == Status::Ok);
        pass(500);
    }

    comms::ConfigService& config() { return rig.product.config_link().config(); }

    void pass(uint32_t for_ms) {
        rig.run(now_ms, now_ms + for_ms);
        now_ms += for_ms + 50;
    }

    std::string say(const std::string& json) {
        const size_t before = rig.platform.link().sent.size();
        rig.send(json.c_str());
        pass(300);
        return answer_since(before);
    }

    std::string answer_since(size_t before) {
        const auto& sent = rig.platform.link().sent;
        std::string answer;
        for (size_t i = before; i < sent.size(); i++) {
            const std::string text = link_text(sent[i].bytes);
            if (sent[i].endpoint == events::Endpoint::Config &&
                text.rfind("{\"command\":\"status\"", 0) != 0)
                answer = text;
        }
        return answer;
    }

    std::string prove(bool trusted) {
        const crypto::Ed25519::PublicKey key = crypto::Ed25519::public_key_of(kBrowserSeed);
        if (trusted) config().trusted_browsers().store(key, "Chrome on Android");
        rig.platform.random().queue(kBrowserNonce);
        say("{\"command\":\"browser_challenge\"}");
        std::string message = link::trust_browser_signed_over::kSignature;
        message.append(reinterpret_cast<const char*>(kBrowserNonce.data()), kBrowserNonce.size());
        const crypto::Ed25519::Signature signature = crypto::Ed25519::sign(
            kBrowserSeed,
            ConstByteSpan(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
        return say("{\"command\":\"trust_browser\",\"key\":\"" +
                   hex_text(ConstByteSpan(key.data(), key.size())) + "\",\"signature\":\"" +
                   hex_text(ConstByteSpan(signature.data(), signature.size())) + "\"}");
    }

    void challenge() {
        rig.platform.random().queue(vectors.bytes<16>("unit_nonce"));
        say("{\"command\":\"challenge\"}");
    }

    std::string register_with(const std::string& signature) {
        return say("{\"command\":\"register\",\"server_nonce\":\"" + vectors.text("server_nonce") +
                   "\",\"signature\":\"" + signature + "\"}");
    }
};

}  // namespace

TEST_CASE("browser register: a trusted browser's register answers the registration at once") {
    Bench bench;
    REQUIRE(bench.prove(/*trusted=*/true).find("\"trusted\"") != std::string::npos);
    bench.challenge();
    const std::string answer = bench.register_with(bench.vectors.text("register_signature"));
    CHECK(answer.rfind("{\"command\":\"registration\"", 0) == 0);
    CHECK(bench.config().pending() == comms::Pending::None);
}

TEST_CASE("browser register: trust skips the press, never the nonce or the server's signature") {
    Bench bench;
    bench.prove(/*trusted=*/true);
    CHECK(bench.register_with(bench.vectors.text("register_signature")) == refusal("no_session"));
    bench.challenge();
    std::string forged = bench.vectors.text("register_signature");
    forged[0] = forged[0] == '0' ? '1' : '0';
    CHECK(bench.register_with(forged) == refusal("bad_signature"));
    CHECK(bench.config().pending() == comms::Pending::None);

    bench.rig.platform.link().declare_payload_bytes(100);
    bench.challenge();
    CHECK(bench.register_with(bench.vectors.text("register_signature")) == refusal("small_mtu"));
}

TEST_CASE("browser register: the press that allows an untrusted browser's register trusts it") {
    Bench bench;
    REQUIRE(bench.prove(/*trusted=*/false).find("\"untrusted\"") != std::string::npos);
    bench.challenge();
    bench.register_with(bench.vectors.text("register_signature"));
    REQUIRE(bench.config().pending() == comms::Pending::Register);
    CHECK(bench.config().prompt_trusts_browser());

    bench.pass(3000);
    const size_t before = bench.rig.platform.link().sent.size();
    bench.rig.press(bench.now_ms);
    bench.pass(500);
    CHECK(bench.answer_since(before).rfind("{\"command\":\"registration\"", 0) == 0);
    CHECK(bench.config().trusted_browsers().count() == 1);
}
