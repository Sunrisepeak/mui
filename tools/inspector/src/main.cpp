// omui-inspector — inspect a running openmui application.
//
//   omui-inspector [--app NAME] [--inspect-dir DIR] [omui::run options: --size, --dark, --headless …]
//
// NAME is an application id as `omui agent ls` lists them; without it, the
// first application that listens (other than the Inspector).
import std;
import openmui;
import openmui.inspector;
import mcpplibs.os;

int main(int argc, char** argv) {
    omui::inspector::Options options;
    bool cellular=false,chinese=false,batch_requested=false;
    std::string argument_error;
    std::filesystem::path batch_file,batch_out;
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--app" && i + 1 < argc) options.app = argv[++i];
        else if (a == "--inspect-dir" && i + 1 < argc) options.directory = argv[++i];
        else if (a == "--cellular") cellular=true;
        else if (a == "--cellular-zh") {cellular=true;chinese=true;}
        else if (a == "--cellular-batch" || a == "--out") {
            if (a=="--cellular-batch") batch_requested=true;
            if (i+1==argc || std::string_view(argv[i+1]).starts_with("--")) argument_error=std::string(a)+" needs a path";
            else {
                auto path=mcpplibs::os::fs::from_utf8(argv[++i]);
                if (a=="--cellular-batch") batch_file=std::move(path);else batch_out=std::move(path);
            }
        }
        else rest.push_back(argv[i]);
    }
    if (batch_requested && batch_file.empty()) argument_error="--cellular-batch needs a nonempty path";
    if (!batch_requested && !batch_out.empty()) argument_error="--out needs --cellular-batch";
    if (!argument_error.empty()) {std::cout<<omui::Value{}.set("ok",false).set("error",argument_error).json()<<'\n';return 2;}
    if (batch_requested) return omui::inspector::cellular::batch(batch_file,batch_out,std::cout);
    if (cellular) {
        const auto session=std::make_shared<omui::inspector::cellular::Session>();
        return omui::run(omui::inspector::cellular::app(session,chinese),static_cast<int>(rest.size()),rest.data());
    }
    return omui::run(omui::inspector::app(std::move(options)), static_cast<int>(rest.size()), rest.data());
}
