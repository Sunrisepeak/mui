import std;
import mlibs.rhi;
import mlibs.validation.rules_consumer;

int main() {
    using namespace consumer::compiled::consumer;
    const bool ok = entries.size() == 2 && consumer_vs.stage == mlibs::rhi::Stage::vertex &&
                    consumer_fs.stage == mlibs::rhi::Stage::fragment && consumer_vs.code.size() == 3 && consumer_fs.code.size() == 3;
    std::println("{{\"kind\":\"mlibs.rules-consumer\",\"ok\":{},\"entries\":{}}}", ok, entries.size());
    return ok ? 0 : 1;
}
