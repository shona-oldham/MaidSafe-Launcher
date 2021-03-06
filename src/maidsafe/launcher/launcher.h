/*  Copyright 2015 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_LAUNCHER_LAUNCHER_H_
#define MAIDSAFE_LAUNCHER_LAUNCHER_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>

#include "boost/filesystem/path.hpp"
#include "boost/optional.hpp"

#include "maidsafe/directory_info.h"
#include "maidsafe/common/asio_service.h"
#include "maidsafe/common/on_scope_exit.h"
#include "maidsafe/common/tcp/connection.h"
#include "maidsafe/passport/passport.h"

#include "maidsafe/launcher/account_handler.h"
#include "maidsafe/launcher/app_handler.h"
#include "maidsafe/launcher/app_details.h"
#include "maidsafe/launcher/types.h"

namespace maidsafe {

namespace launcher {

class AccountGetter;
struct Launch;

// Unless otherwise indicated, this class' public functions all throw on error and provide the
// strong exception-safety guarantee.
//
// An app which has been added to the Launcher on this machine for this user is known as a local or
// locally-available app.  An app which has been added for this user via a Launcher on a different
// machine is known as a non-local or non-locally-available app.  The set of local and non-local
// apps are mutually-exclusive.
//
// A non-local app can be added locally by calling 'LinkApp', not 'AddApp'.
class Launcher {
 public:
  Launcher(const Launcher&) = delete;
  Launcher(Launcher&&) = delete;
  Launcher& operator=(const Launcher&) = delete;
  Launcher& operator=(Launcher&&) = delete;

  // Retrieves and decrypts account info and starts a new session by logging into the network.
  static std::unique_ptr<Launcher> Login(Keyword keyword, Pin pin, Password password);

  // This function should be used when creating a new account, i.e. where an account has never
  // been put to the network.  Creates a new account, encrypts it and puts it to the network.
  static std::unique_ptr<Launcher> CreateAccount(Keyword keyword, Pin pin, Password password);

  // Saves session, and logs out of the network.  After calling, the class should be destructed as
  // it is no longer connected to the network.
  void LogoutAndStop();

  // Returns the set of apps which have been added; either the locally-available ones or the
  // non-locally-available ones depending on the value of 'locally_available'.
  std::set<AppDetails> GetApps(bool locally_available) const;

  // Adds an instance of 'app_name' to the set of local apps.  Throws if the app has already been
  // added locally or non-locally.  (To add an app which has previously been added non-locally, use
  // the 'LinkApp' function.)
  void AddApp(AppName app_name, boost::filesystem::path app_path, AppArgs app_args,
              SerialisedData app_icon, bool auto_start);

  // Adds an instance of 'app_name' to the set of local apps where this app must have been
  // previously added non-locally.  Throws if the app has already been added locally, linked, or has
  // *not* been added non-locally.
  void LinkApp(AppName app_name, boost::filesystem::path app_path, AppArgs app_args,
               bool auto_start);

  // The 'Update...' functions all replace the existing field with the new one for the app indicated
  // by 'app_name'.
  void UpdateAppName(const AppName& app_name, const AppName& new_name);
  void UpdateAppPath(const AppName& app_name, const boost::filesystem::path& new_path);
  void UpdateAppArgs(const AppName& app_name, const AppArgs& new_args);
  void UpdateAppSafeDriveAccess(const AppName& app_name, DirectoryInfo::AccessRights new_rights);
  void UpdateAppIcon(const AppName& app_name, const SerialisedData& new_icon);
  void UpdateAppAutoStart(const AppName& app_name, bool new_auto_start_value);

  // Removes an instance of the app indicated by 'app_name' from the set of locally-available apps.
  // Throws if the app isn't in the set.
  void RemoveAppLocally(const AppName& app_name);

  // Removes an instance of the app indicated by 'app_name' from the set of non-locally available
  // apps.  Throws if the app isn't in the set.
  void RemoveAppFromNetwork(const AppName& app_name);

  // Save the account to the network.  If 'force' is false, the account is only saved if there are
  // unsaved changes in the account (e.g. if AddApp has been called).  If 'force' is true, the
  // account is saved unconditionally.  If the functions throws an exception indicating a temporary
  // problem, it is safe to retry SaveSession, otherwise the user probably needs to take action.
  void SaveSession(bool force = false);

  // Reverts the internal state back to the last successful 'SaveSession' call, or the initial state
  // if there have been no 'SaveSession' calls.
  void RevertToLastSavedSession();

  // Launches a new instance of the app indicated by 'app_name' as a detached child.
  //
  // The app will be passed the Launcher's TCP listening port in a command line argument
  // "--launcher_port=X" where X will be a random port between 1025 and 65535 inclusive.  The app
  // must then establish a TCP connection to the launcher on the loopback address at this port
  // within the 'connect_timeout_' duration or the launch attempt fails.
  //
  // Once the connection is established, the app should immediately pass through its session public
  // key and wait for the Launcher to reply with the set of NFS directories to which it has access.
  // The app should then reply to confirm receipt, at which time the connection is closed and the
  // app is orphaned so that it no longer depends on the Launcher running.
  //
  // The time from the connection being established until the Launcher receives the final
  // confirmation from the app must be within the 'handshake_timeout_' duration or the launch fails.
  //
  // For apps, there is a blocking function to handle this entire process in the API project named
  // 'RegisterAppSession'.
  void LaunchApp(const AppName& app_name);

  static const std::chrono::steady_clock::duration connect_timeout_;
  static const std::chrono::steady_clock::duration handshake_timeout_;

#ifdef USE_FAKE_STORE
  static boost::filesystem::path FakeStorePath(
      const boost::filesystem::path* const disk_path = nullptr);
  static DiskUsage FakeStoreDiskUsage(const DiskUsage* const disk_usage = nullptr);
#endif

 private:
  // For already existing accounts.
  Launcher(Keyword keyword, Pin pin, Password password, AccountGetter& account_getter);

  // For new accounts.  Throws on failure to create account.
  Launcher(Keyword keyword, Pin pin, Password password, passport::MaidAndSigner&& maid_and_signer);

  void AddOrLinkApp(AppName app_name, boost::filesystem::path app_path, AppArgs app_args,
                    const SerialisedData* const app_icon, bool auto_start);

  void RevertAppHandler(AppHandler::Snapshot snapshot);

  void LaunchApp(const AppName& app_name, const boost::filesystem::path& path, AppArgs args);

  void HandleNewConnection(std::shared_ptr<Launch> launch, tcp::ConnectionPtr connection);

  void HandleMessage(std::shared_ptr<Launch> launch, tcp::Message message);

  AsioService asio_service_;
  std::shared_ptr<NetworkClient> network_client_;
  AccountHandler account_handler_;
  mutable std::mutex account_mutex_;
  AppHandler app_handler_;
  boost::optional<AppHandler::Snapshot> rollback_snapshot_;
};

}  // namespace launcher

}  // namespace maidsafe

#endif  // MAIDSAFE_LAUNCHER_LAUNCHER_H_
