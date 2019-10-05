#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace {
    // define some constants we use
    constexpr size_t MB = 1024*1024;
    constexpr size_t BUFSZ = MB;
    const char *VER = "1.0"; // program version

    volatile bool interrupted = false; // flag set when SIGINT received

    struct Context
    {
        std::string outfile;
        size_t mb = 2*1024;  // 2 GB default size
        bool valid = false, outfileCreated = false;

        operator bool() const { return valid; }
    };

    Context parseArgs(int argc, const char * const * argv);

    // returns relative time since program start in seconds (uses high precision clock)
    double getTime();

    int doRead(const Context & p);
    int doWrite(Context & p);

    // Kind of like Go's "defer" statement. Call a functor (for clean-up code) at scope end.
    struct Defer
    {
        std::function<void(void)> func;

        Defer(const std::function<void(void)> & f) : func(f) {}
        Defer(std::function<void(void)> && f) : func(std::move(f)) {}

        ~Defer() { if (func) func(); }
    };

    void sigHandler(int sig)
    {
        interrupted = true;
        std::cerr << "(Caught signal " << sig << ", will exit)" << std::endl;
    }
}

int main(int argc, char **argv)
{
    Context p = parseArgs(argc, argv);

    if ( ! p ) {
        return 1;
    }

    // install signal handler
    ::signal(SIGINT, sigHandler);
    ::signal(SIGQUIT, sigHandler);
    ::signal(SIGTERM, sigHandler);
    ::signal(SIGHUP, sigHandler);

    Defer defer_RmOutfile([&p]{
        if (p.outfileCreated) {
            if (unlink(p.outfile.c_str())) {
                std::cerr << "Failed to remove file " << p.outfile << std::endl;
            } else {
                p.outfileCreated = false;
                std::cerr << "(Removed " << p.outfile << ")" << std::endl;
            }
        }
    });

    int res;
    res = doWrite(p);
    if (res)
        return res;
    res = doRead(p);

    return res;
}


namespace {

    int doRead(const Context & p)
    {
        std::cout << "Running /usr/sbin/purge with sudo (clearing read cache)..." << std::endl;
        // purge command clears read caches
        int res = std::system("/usr/bin/sudo /usr/sbin/purge");
        if (res) {
            std::cerr << "Failed to execute purge, exit code: " << res << std::endl;
            return res;
        }
        std::cout << "Reading back " << p.outfile << "..." << std::flush;
        int fd = ::open(p.outfile.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            std::cerr << "\nError opening file" << std::endl;
            return 10;
        }

        Defer defer_CloseFd([&fd]{
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        });

        res = ::fcntl(fd, F_NOCACHE, 1);  // turn off read cache
        if (res) {
            std::cerr << "\nfcntl(F_NOCACHE) returned " << res << std::endl;
            return 11;
        }

        auto buf = std::make_unique<char[]>(BUFSZ); // we allocate data on the heap, BUFSZ bytes
        size_t count = 0;
        ssize_t nread = 0;

        double t0 = getTime();

        while ( (nread = ::read(fd, buf.get(), BUFSZ)) > 0 && !interrupted) {
            count += nread;
        }

        if (interrupted)
            return 99;

        if (count) {
            const double elapsed = getTime() - t0;
            const double n_MB = count/double(MB);
            std::cout << "took " << std::fixed << std::setprecision(3) << elapsed << " secs (" << std::setprecision(2) << (n_MB/elapsed) << " MB/sec)" << std::endl;
        } else {
            std::cerr << "Error reading!" << std::endl;
            return 20;
        }

        return 0;
    }

    int doWrite(Context & p)
    {
        const size_t N = p.mb * MB;

        if (N < BUFSZ) {
            std::cerr << "Invalid output size specified: " << N << std::endl;
            return 2;
        }

        std::ofstream ostrm;
        ostrm.exceptions(std::ofstream::failbit | std::ofstream::badbit); // throw on open or write failure

        double t0; // starts off uninitialized but will be initialized once we begin writing below...

        try {
            ostrm.open(p.outfile, std::ios::binary | std::ios::out | std::ios::trunc);
            p.outfileCreated = true;

            auto buf = std::make_unique<char[]>(BUFSZ); // we allocate data on the heap, BUFSZ bytes

            {   // assign random data to buf
                std::cout << "Generating random data..." << std::flush;
                double t0 = getTime();
                std::random_device rd;
                std::uniform_int_distribution<uint64_t> dist(0);
                uint64_t *words = reinterpret_cast<uint64_t *>(buf.get());
                for (size_t i = 0; i < BUFSZ/sizeof(*words); ++i) {
                    words[i] = dist(rd);
                }
                std::cout << "took " << std::fixed << std::setprecision(3) << (getTime()-t0) << " seconds" << std::endl;
            }

            std::cout << "Writing " << p.mb << " MB to " << p.outfile << "..." << std::flush;

            t0 = getTime(); // mark write start time

            for (size_t i = 0; i < N/BUFSZ && !interrupted; ++i) {
                ostrm.write(buf.get(), BUFSZ);
            }
            ostrm.flush();
            ostrm.close();
        } catch (const std::ios_base::failure &e) {
            const auto verb = ostrm.is_open() ? "writing to" : "opening";
            std::cerr << "Error " << verb << " " << p.outfile << " (" << e.what() << ")" << std::endl;
            return 3;
        }

        if (interrupted)
            return 99;

        const double elapsed = getTime() - t0;
        const double mbsec = p.mb / elapsed;

        std::cout << "took " << std::fixed << std::setprecision(3) << elapsed << " seconds"
                  << " (" << std::setprecision(2) << mbsec << " MB/sec)" << std::endl;

        return 0;
    }

    Context parseArgs(int argc, const char * const * argv)
    {
        Context p;

        auto Inner = [&]() -> bool {

            auto usage = [argc, argv](bool showBanner = true) {
                const char *progname = argc ? argv[0] : "wrdata";
                if (showBanner) {
                    std::cerr << "OSX Simple SSD Benchmark " << VER << std::endl;
                    std::cerr << "© 2019 Calin Culianu <calin.culianu@gmail.com>" << std::endl << std::endl;
                }
                std::cerr << "Usage: \t" << progname << " outfile" << " [SIZE_MB]" << std::endl;
                if (showBanner) {
                    std::cerr << std::endl; // additional newline if banner mode
                }
            };

            if (argc < 2) {
                usage();
                return false;
            }

            // parse outfile
            p.outfile = argv[1];

            if (!p.outfile.length() || p.outfile[0] == '-') {
                usage();
                return false;
            }

            // parse MB
            if (argc > 2) {
                try {
                    size_t pos = 0;
                    std::string s(argv[2]);
                    long mb = std::stol(s, &pos);
                    if (mb <= 0)
                        throw std::runtime_error("must be >= 0");
                    if (pos < s.length())
                        throw std::runtime_error("extra characters at end of string");
                    p.mb = mb;
                } catch (const std::exception & e) {
                    std::cerr << "Failed to parse SIZE_MB (" << e.what() << ")\n" << std::endl;
                    usage(false);
                    return false;
                }
            }

            return true;
        };

        p.valid = Inner();
        return p;
    }

    double getTime()
    {
        static auto t0 = std::chrono::high_resolution_clock::now();

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = now-t0;
        return diff.count();
    }

} // end anonymous namespace
