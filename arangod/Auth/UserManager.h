////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2017 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_AUTHENTICATION_USER_MANAGER_H
#define ARANGOD_AUTHENTICATION_USER_MANAGER_H 1

#include "Auth/Common.h"
#include "Auth/User.h"

#include "ApplicationFeatures/ApplicationFeature.h"
#include "Basics/LruCache.h"
#include "Basics/Mutex.h"
#include "Basics/ReadWriteLock.h"
#include "Basics/Result.h"
#include "Rest/CommonDefines.h"

#include <velocypack/Builder.h>
#include <velocypack/Slice.h>

namespace arangodb {
namespace aql {
  class QueryRegistry;
}
  
namespace auth {
class Handler;

typedef std::unordered_map<std::string, auth::User> UserMap;

/// UserManager is the sole point of access for users and permissions
/// stored in `_system/_users` as well as in external authentication
/// systems like LDAP. The permissions are cached locally if possible,
/// to avoid unecessary disk access.
class UserManager {
 public:
  explicit UserManager(std::unique_ptr<arangodb::auth::Handler>&&);
  ~UserManager();

 public:
  typedef std::function<void(auth::User&)> UserCallback;
  typedef std::function<void(auth::User const&)> ConstUserCallback;
  
  void setQueryRegistry(aql::QueryRegistry* registry) {
    TRI_ASSERT(registry != nullptr);
    _queryRegistry = registry;
  }

  /// Tells coordinator to reload its data. Only called in HeartBeat thread
  void outdate() { _outdated = true; }

  /// Trigger eventual reload, user facing API call
  void reloadAllUsers();

  /// Create the root user with a default password, will fail if the user
  /// already exists. Only ever call if you can guarantee to be in charge
  void createRootUser();

  velocypack::Builder allUsers();
  /// Add user from arangodb, do not use for LDAP  users
  Result storeUser(bool replace, std::string const& user,
                   std::string const& pass, bool active);
  
  /// Enumerate list of all users
  Result enumerateUsers(UserCallback const&);
  /// Update specific user
  Result updateUser(std::string const& user, UserCallback const&);
  /// Access user without modifying it
  Result accessUser(std::string const& user, ConstUserCallback const&);
  
  velocypack::Builder serializeUser(std::string const& user);
  Result removeUser(std::string const& user);
  Result removeAllUsers();

  velocypack::Builder getConfigData(std::string const& user);
  Result setConfigData(std::string const& user, velocypack::Slice const& data);
  velocypack::Builder getUserData(std::string const& user);
  Result setUserData(std::string const& user, velocypack::Slice const& data);

  bool checkPassword(std::string const& username,
                     std::string const& password);

  
  auth::Level configuredDatabaseAuthLevel(std::string const& username,
                                        std::string const& dbname);
  auth::Level configuredCollectionAuthLevel(std::string const& username,
                                          std::string const& dbname,
                                          std::string coll);
  auth::Level canUseDatabase(std::string const& username,
                             std::string const& dbname);
  auth::Level canUseCollection(std::string const& username,
                             std::string const& dbname,
                             std::string const& coll);

  // No Lock variants of the above to be used in callbacks
  // Use with CARE! You need to make sure that the lock
  // is held from outside.
  auth::Level canUseDatabaseNoLock(std::string const& username,
                                   std::string const& dbname);
  auth::Level canUseCollectionNoLock(std::string const& username,
                                   std::string const& dbname,
                                   std::string const& coll);

  /// Overwrite internally cached permissions, only use
  /// for testing purposes
  void setAuthInfo(auth::UserMap const& userEntryMap);

 private:
  // worker function for canUseDatabase
  // must only be called with the read-lock on _authInfoLock being held
  auth::Level configuredDatabaseAuthLevelInternal(std::string const& username,
                                   std::string const& dbname, size_t depth) const;

  // internal method called by canUseCollection
  // asserts that collection name is non-empty and already translated
  // from collection id to name
  auth::Level configuredCollectionAuthLevelInternal(std::string const& username,
                                                    std::string const& dbname,
                                                    std::string const& coll,
                                                    size_t depth) const;
  void loadFromDB();
  bool parseUsers(velocypack::Slice const& slice);
  Result storeUserInternal(auth::User const& user, bool replace);

 private:
  basics::ReadWriteLock _authInfoLock;
  Mutex _loadFromDBLock;
  std::atomic<bool> _outdated;

  UserMap _authInfo;
  
  aql::QueryRegistry* _queryRegistry;
  std::unique_ptr<arangodb::auth::Handler> _authHandler;
};
} // auth
} // arangodb

#endif