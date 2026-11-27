// oweb-reader-reading: the same program without what runs a page's live examples — what a page that holds none
// needs. Its functions are the world's first part (build.mcpp's `parts`); the rest, the examples', is fetched only by a
// page whose document holds one.
import oweb.reader;

int main(int argc, char** argv) { return oweb::reader::program(argc, argv, nullptr); }
