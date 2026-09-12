/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/Try.h>
#include <LibCore/Socket.h>
#include <LibCore/System.h>
#include <LibIPC/Transport.h>

template<typename Client>
ErrorOr<NonnullRefPtr<Client>> bind_service(void (*bind_method)(int))
{
    int socket_fds[2] {};
    TRY(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fds));

    int ui_fd = socket_fds[0];
    int server_fd = socket_fds[1];

    // NOTE: The java object takes ownership of the socket fds
    (*bind_method)(server_fd);

    auto socket = TRY(Core::LocalSocket::adopt_fd(ui_fd));
    TRY(socket->set_blocking(true));

    auto new_client = TRY(try_make_ref_counted<Client>(make<IPC::Transport>(move(socket))));

    return new_client;
}

void bind_request_server_java(int ipc_socket);
void bind_image_decoder_java(int ipc_socket);
