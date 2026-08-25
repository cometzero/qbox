/* SPDX-License-Identifier: BSD-3-Clause-Clear */

#ifndef _LIBQBOX_RUNTIME_ACTION_API_H
#define _LIBQBOX_RUNTIME_ACTION_API_H

#include <string>

#include "crow.h"
#include "runtime-action-service.h"

namespace gs {
namespace runtime_action_api {

crow::json::wvalue capability_json(const RuntimeTargetCapability& capability);
crow::json::wvalue snapshot_json(const RuntimeTargetSnapshot& snapshot);
crow::json::wvalue status_json(const RuntimeActionStatus& status);
crow::response json_response(int status, crow::json::wvalue body);
crow::response error_response(int status, const std::string& code, const std::string& message);
crow::response status_reply_response(const RuntimeActionStatusReply& reply);
bool parse_action_request(const std::string& body, RuntimeActionRequest& request, std::string& error);

}
}

#endif
