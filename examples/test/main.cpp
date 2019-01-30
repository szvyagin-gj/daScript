#include "daScript/daScript.h"

#include <fstream>

#ifdef _MSC_VER
    #include <io.h>
#else
    #include <dirent.h>
#endif

using namespace das;

bool g_reportCompilationFailErrors = false;

TextPrinter tout;

bool compilation_fail_test ( const string & fn ) {
    tout << fn << " ";
    std::string str;
    std::ifstream t(fn.c_str());
    if ( !t.is_open() ) {
        tout << "not found\n";
        return false;
    }
    t.seekg(0, std::ios::end);
    str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);
    str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    if ( auto program = parseDaScript(str.c_str(), tout) ) {
        if ( program->failed() ) {
            bool failed = false;
            auto errors = program->expectErrors;
            for ( auto err : program->errors ) {
                int count = -- errors[err.cerr];
                if ( g_reportCompilationFailErrors || count<0 ) {
                    tout << reportError(str.c_str(), err.at.line, err.at.column, err.what, err.cerr );
                }
                if ( count <0 ) {
                    failed = true;
                }
            }
            bool any_errors = false;
            for ( auto err : errors ) {
                if ( err.second > 0 ) {
                    any_errors = true;
                    break;
                }
            }
            if ( any_errors || failed ) {
                tout << "failed";
                if ( any_errors ) {
                    tout << ", expecting errors";
                    for ( auto terr : errors  ) {
                        if (terr.second > 0 ) {
                            tout << " " << int(terr.first) << ":" << terr.second;
                        }
                    }
                }
                tout << "\n";
                return false;
            }
            tout << "ok\n";
            return true;
        } else {
            tout << "failed, compiled without errors\n";
            return false;
        }
    } else {
        tout << "failed, not expected to compile\n";
        return false;
    }
}

bool unit_test ( const string & fn ) {
    tout << fn << " ";
    std::string str;
    std::ifstream t(fn.c_str());
    if ( !t.is_open() ) {
        tout << "not found\n";
        return false;
    }
    t.seekg(0, std::ios::end);
    str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);
    str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    if ( auto program = parseDaScript(str.c_str(), tout) ) {
        if ( program->failed() ) {
            tout << "failed to compile\n";
            for ( auto & err : program->errors ) {
                tout << reportError(str.c_str(), err.at.line, err.at.column, err.what, err.cerr );
            }
            return false;
        } else {
            string str2;
            Context ctx(&str2);
            program->simulate(ctx, tout);
            if ( auto fnTest = ctx.findFunction("test") ) {
                ctx.restart();
                bool result = cast<bool>::to(ctx.eval(fnTest, nullptr));
                if ( auto ex = ctx.getException() ) {
                    tout << "exception: " << ex << "\n";
                    return false;
                }
                if ( !result ) {
                    tout << "failed\n";
                    return false;
                }
                tout << "ok\n";
                return true;
            } else {
                tout << "function 'test' not found\n";
                return false;
            }
        }
    } else {
        return false;
    }
}

bool exception_test ( const string & fn ) {
    tout << fn << " ";
    std::string str;
    std::ifstream t(fn.c_str());
    if ( !t.is_open() ) {
        tout << "not found\n";
        return false;
    }
    t.seekg(0, std::ios::end);
    str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);
    str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
    if ( auto program = parseDaScript(str.c_str(), tout) ) {
        if ( program->failed() ) {
            tout << "failed to compile\n";
            for ( auto & err : program->errors ) {
                tout << reportError(str.c_str(), err.at.line, err.at.column, err.what, err.cerr );
            }
            return false;
        } else {
            string str2;
            Context ctx(&str2);
            program->simulate(ctx, tout);
            if ( auto fnTest = ctx.findFunction("test") ) {
                ctx.restart();
                ctx.evalWithCatch(fnTest, nullptr);
                if ( auto ex = ctx.getException() ) {
                    tout << "ok\n";
                    return true;
                }
                tout << "failed, finished without exception\n";
                return false;
            } else {
                tout << "function 'test' not found\n";
                return false;
            }
        }
    } else {
        return false;
    }
}

bool run_tests( const string & path, bool (*test_fn)(const string &) ) {
#ifdef _MSC_VER
    bool ok = true;
    _finddata_t c_file;
    intptr_t hFile;
    string findPath = path + "/*.das";
    if ((hFile = _findfirst(findPath.c_str(), &c_file)) != -1L) {
        do {
            ok = test_fn(path + "/" + c_file.name) && ok;
        } while (_findnext(hFile, &c_file) == 0);
    }
    _findclose(hFile);
    return ok;
#else
    bool ok = true;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir (path.c_str())) != NULL) {
        while ((ent = readdir (dir)) != NULL) {
            if ( strstr(ent->d_name,".das") ) {
                ok = test_fn(path + "/" + ent->d_name) && ok;
            }
        }
        closedir (dir);
    }
    return ok;
#endif
}

bool run_unit_tests( const string & path ) {
    return run_tests(path, unit_test);
}

bool run_compilation_fail_tests( const string & path ) {
    return run_tests(path, compilation_fail_test);
}

bool run_exception_tests( const string & path ) {
    return run_tests(path, exception_test);
}

int main() {
#ifdef _MSC_VER
    #define    TEST_PATH "../"
#else
    #define TEST_PATH "../../"
#endif
    // register modules
    NEED_MODULE(Module_BuiltIn);
    NEED_MODULE(Module_Math);
    NEED_MODULE(Module_UnitTest);
#if 0 // Debug this one test
    compilation_fail_test(TEST_PATH "test/compilation_fail_tests/static_assert_in_infer.das");
    Module::Shutdown();
    return 0;
#endif
#if 0 // Debug this one test
    unit_test(TEST_PATH "test/hello_world.das");
    Module::Shutdown();
    return 0;
#endif
    bool ok = true;
    ok = run_compilation_fail_tests(TEST_PATH "test/compilation_fail_tests") && ok;
    ok = run_unit_tests(TEST_PATH "test/unit_tests") && ok;
    ok = run_unit_tests(TEST_PATH "test/optimizations") && ok;
    ok = run_exception_tests(TEST_PATH "test/runtime_errors") && ok;
    tout << "TESTS " << (ok ? "PASSED" : "FAILED!!!") << "\n";
    // shutdown
    Module::Shutdown();
    return ok ? 0 : -1;
}