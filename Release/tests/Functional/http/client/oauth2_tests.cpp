/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved. 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* oauth2_tests.cpp
*
* Test cases for oauth2.
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/

#include "stdafx.h"

using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::details;
using namespace utility;
using namespace concurrency;

using namespace tests::functional::http::utilities;

namespace tests { namespace functional { namespace http { namespace client {

SUITE(oauth2_tests)
{

class oauth2_test_uri
{
public:
    oauth2_test_uri() : m_uri(U("http://localhost:16743/")) {}
    web::http::uri m_uri;
};

TEST(oauth2_build_authorization_uri)
{
    oauth2_config c(U(""), U(""), U(""), U(""), U(""));
    c.set_state(U("xyzzy"));
    c.set_implicit_grant(false);

    // Empty authorization URI.
    {
        VERIFY_ARE_EQUAL(U("/?response_type=code&client_id=&redirect_uri=&state=xyzzy"), c.build_authorization_uri(false));
    }

    // Authorization URI with scope parameter.
    {
        c.set_scope(U("testing_123"));
        VERIFY_ARE_EQUAL(U("/?response_type=code&client_id=&redirect_uri=&state=xyzzy&scope=testing_123"), c.build_authorization_uri(false));
    }

    // Full authorization URI with scope.
    {
        c.set_client_key(U("4567abcd"));
        c.set_auth_endpoint(U("https://foo"));
        c.set_redirect_uri(U("http://localhost:8080"));
        VERIFY_ARE_EQUAL(U("https://foo/?response_type=code&client_id=4567abcd&redirect_uri=http://localhost:8080&state=xyzzy&scope=testing_123"),
                c.build_authorization_uri(false));
    }

    // Verify again with implicit grant.
    {
        c.set_implicit_grant(true);
        VERIFY_ARE_EQUAL(U("https://foo/?response_type=token&client_id=4567abcd&redirect_uri=http://localhost:8080&state=xyzzy&scope=testing_123"),
                c.build_authorization_uri(false));
    }

    // Verify that a new state() will be generated.
    {
        const uri auth_uri(c.build_authorization_uri(true));
        auto params = uri::split_query(auth_uri.query());
        VERIFY_ARE_NOT_EQUAL(params[U("state")], U("xyzzy"));
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_token_from_code)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U("123ABC"), U("456DEF"), U("https://foo"), m_uri.to_string(), U("https://bar"));

    VERIFY_ARE_EQUAL(false, c.is_enabled());

    // Fetch using HTTP Basic authentication.
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(request->m_method, methods::POST);

            utility::string_t content, charset;
            parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);
            VERIFY_ARE_EQUAL(mime_types::application_x_www_form_urlencoded, content);

            VERIFY_ARE_EQUAL(U("Basic MTIzQUJDOjQ1NkRFRg=="), request->m_headers[header_names::authorization]);

            VERIFY_ARE_EQUAL(conversions::to_body_data(
                    U("grant_type=authorization_code&code=789GHI&redirect_uri=https%3A%2F%2Fbar")),
                    request->m_body);

            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"xyzzy123\",\"token_type\":\"bearer\"}");
        });

        c.token_from_code(U("789GHI")).wait();
        VERIFY_ARE_EQUAL(U("xyzzy123"), c.token().access_token());
        VERIFY_ARE_EQUAL(true, c.is_enabled());
    }

    // Fetch using client key & secret in request body (x-www-form-urlencoded).
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            utility::string_t content;
            utility::string_t charset;
            parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);
            VERIFY_ARE_EQUAL(mime_types::application_x_www_form_urlencoded, content);

            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);

            VERIFY_ARE_EQUAL(conversions::to_body_data(
                    U("grant_type=authorization_code&code=789GHI&redirect_uri=https%3A%2F%2Fbar&client_id=123ABC&client_secret=456DEF")),
                    request->m_body);

            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"xyzzy123\",\"token_type\":\"bearer\"}");
        });

        c.set_token(oauth2_token()); // Clear token.
        VERIFY_ARE_EQUAL(false, c.is_enabled());
        c.set_http_basic_auth(false);
        c.token_from_code(U("789GHI")).wait();
        VERIFY_ARE_EQUAL(U("xyzzy123"), c.token().access_token());
        VERIFY_ARE_EQUAL(true, c.is_enabled());
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_token_from_redirected_uri)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U("X"), U("Y"), U("https://foo"), m_uri.to_string(), U("https://bar"));

    // Authorization code grant.
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"foo\",\"token_type\":\"bearer\"}");
        });
    
        c.set_implicit_grant(false);
        c.set_state(U("xyzzy"));

        const web::http::uri redirected_uri(m_uri.to_string() + U("?code=sesame&state=xyzzy"));
        c.token_from_redirected_uri(redirected_uri).wait();

        VERIFY_IS_TRUE(c.token().is_valid());
        VERIFY_ARE_EQUAL(c.token().access_token(), U("foo"));
    }

    // Implicit grant.
    {
        c.set_implicit_grant(true);
        const web::http::uri redirected_uri(m_uri.to_string() + U("#access_token=abcd1234&state=xyzzy"));
        c.token_from_redirected_uri(redirected_uri).wait();

        VERIFY_IS_TRUE(c.token().is_valid());
        VERIFY_ARE_EQUAL(c.token().access_token(), U("abcd1234"));
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_token_from_refresh)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U("123ABC"), U("456DEF"), U("https://foo"), m_uri.to_string(), U("https://bar"));

    oauth2_token token(U("accessing"));
    token.set_refresh_token(U("refreshing"));
    c.set_token(token);
    VERIFY_ARE_EQUAL(true, c.is_enabled());

    // Verify token refresh without scope.
    scoped.server()->next_request().then([](test_request *request)
    {
        VERIFY_ARE_EQUAL(request->m_method, methods::POST);

        utility::string_t content, charset;
        parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);
        VERIFY_ARE_EQUAL(mime_types::application_x_www_form_urlencoded, content);

        VERIFY_ARE_EQUAL(U("Basic MTIzQUJDOjQ1NkRFRg=="), request->m_headers[header_names::authorization]);

        VERIFY_ARE_EQUAL(conversions::to_body_data(
                U("grant_type=refresh_token&refresh_token=refreshing")),
                request->m_body);

        std::map<utility::string_t, utility::string_t> headers;
        headers[header_names::content_type] = mime_types::application_json;
        request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"ABBA\",\"refresh_token\":\"BAZ\",\"token_type\":\"bearer\"}");
    });

    c.token_from_refresh().wait();
    VERIFY_ARE_EQUAL(U("ABBA"), c.token().access_token());
    VERIFY_ARE_EQUAL(U("BAZ"), c.token().refresh_token());

    // Verify chaining refresh tokens and refresh with scope.
    scoped.server()->next_request().then([](test_request *request)
    {
        utility::string_t content, charset;
        parse_content_type_and_charset(request->m_headers[header_names::content_type], content, charset);

        VERIFY_ARE_EQUAL(conversions::to_body_data(
                U("grant_type=refresh_token&refresh_token=BAZ&scope=xyzzy")),
                request->m_body);

        std::map<utility::string_t, utility::string_t> headers;
        headers[header_names::content_type] = mime_types::application_json;
        request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"done\",\"token_type\":\"bearer\"}");
    });
    
    c.set_scope(U("xyzzy"));
    c.token_from_refresh().wait();
    VERIFY_ARE_EQUAL(U("done"), c.token().access_token());
}

TEST_FIXTURE(oauth2_test_uri, oauth2_bearer_token)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(oauth2_token(U("12345678")));
    http_client_config config;

    // Default, bearer token in "Authorization" header (bearer_auth() == true)
    {
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U("Bearer 12345678"), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }

    // Bearer token in query, default access token key (bearer_auth() == false)
    {
        c.set_bearer_auth(false);
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/?access_token=12345678"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }

    // Bearer token in query, updated token, custom access token key (bearer_auth() == false)
    {
        c.set_bearer_auth(false);
        c.set_access_token_key(U("open"));
        c.set_token(oauth2_token(U("Sesame")));
        config.set_oauth2(c);

        http_client client(m_uri, config);
        scoped.server()->next_request().then([](test_request *request)
        {
            VERIFY_ARE_EQUAL(U(""), request->m_headers[header_names::authorization]);
            VERIFY_ARE_EQUAL(U("/?open=Sesame"), request->m_path);
            request->reply(status_codes::OK);
        });

        http_response response = client.request(methods::GET).get();
        VERIFY_ARE_EQUAL(status_codes::OK, response.status_code());
    }
}

TEST_FIXTURE(oauth2_test_uri, oauth2_token_parsing)
{
    test_http_server::scoped_server scoped(m_uri);
    oauth2_config c(U(""), U(""), U("https://foo"), m_uri.to_string(), U("https://bar"));

    VERIFY_ARE_EQUAL(false, c.is_enabled());

    // Verify reply JSON 'access_token', 'refresh_token', 'expires_in' and 'scope'.
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"123\",\"refresh_token\":\"ABC\",\"token_type\":\"bearer\",\"expires_in\":12345678,\"scope\":\"baz\"}");
        });

        c.token_from_code(U("")).wait();
        VERIFY_ARE_EQUAL(U("123"), c.token().access_token());
        VERIFY_ARE_EQUAL(U("ABC"), c.token().refresh_token());
        VERIFY_ARE_EQUAL(12345678, c.token().expires_in());
        VERIFY_ARE_EQUAL(U("baz"), c.token().scope());
        VERIFY_ARE_EQUAL(true, c.is_enabled());
    }

    // Verify undefined 'expires_in' and 'scope'.
    {
        scoped.server()->next_request().then([](test_request *request)
        {
            std::map<utility::string_t, utility::string_t> headers;
            headers[header_names::content_type] = mime_types::application_json;
            request->reply(status_codes::OK, U(""), headers, "{\"access_token\":\"123\",\"token_type\":\"bearer\"}");
        });

        const utility::string_t test_scope(U("wally world"));
        c.set_scope(test_scope);

        c.token_from_code(U("")).wait();
        VERIFY_ARE_EQUAL(oauth2_token::undefined_expiration, c.token().expires_in());
        VERIFY_ARE_EQUAL(test_scope, c.token().scope());
    }
}

} // SUITE(oauth2_tests)


}}}}
