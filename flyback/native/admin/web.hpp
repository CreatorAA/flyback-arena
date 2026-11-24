#pragma once
#include "../common/util.hpp"
#include <memory>
#include <vector>
namespace fbar {
class Admin {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    Admin(const Config &, J initial);
    ~Admin();
    std::vector<J> commands();
    void publish(J);
    void event(std::string, J);
    int port() const;
    static void set_user(const Config &, const std::string &name, const std::string &role,
                         const std::string &password);
};
std::string utc_stamp();
} // namespace fbar
