/*
 * Verda Ŝtelo - An anagram game in Esperanto for the web
 * Copyright (C) 2022  Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vsx-generate-qr.h"
#include "vsx-id-url.h"
#include "vsx-util.h"

#ifdef HAVE_FASTCGI

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

static bool
open_fastcgi_socket(const char *filename)
{
        int res;

        res = unlink(filename);

        if (res == -1 && errno != ENOENT) {
                fprintf(stderr,
                        "error deleting %s: %s\n",
                        filename,
                        strerror(errno));
                return false;
        }

        int sock = socket(PF_LOCAL, SOCK_STREAM, 0);

        struct sockaddr_un *sockaddr_un =
                vsx_alloc(offsetof(struct sockaddr_un, sun_path) +
                          strlen(filename) + 1);

        sockaddr_un->sun_family = AF_LOCAL;
        strcpy(sockaddr_un->sun_path, filename);

        res = bind(sock,
                   (struct sockaddr *) sockaddr_un,
                   offsetof(struct sockaddr_un, sun_path) +
                   strlen(filename));

        vsx_free(sockaddr_un);

        if (res == -1) {
                fprintf(stderr,
                        "error binding to %s: %s\n",
                        filename,
                        strerror(errno));
                vsx_close(sock);
                return false;
        }

        res = chmod(filename,
                    S_IRUSR | S_IWUSR | S_IXUSR |
                    S_IRGRP | S_IWGRP | S_IXGRP |
                    S_IROTH | S_IWOTH | S_IXOTH);

        if (res == -1) {
                fprintf(stderr,
                        "error setting permissions on %s: %s\n",
                        filename,
                        strerror(errno));
                vsx_close(sock);
                return false;
        }

        res = listen(sock, 10);

        if (res == -1) {
                fprintf(stderr,
                        "error listening on %s: %s\n",
                        filename,
                        strerror(errno));
                vsx_close(sock);
                return false;
        };

        res = dup2(sock, STDIN_FILENO);

        vsx_close(sock);

        if (res == -1) {
                fprintf(stderr,
                        "dup2: %s\n",
                        strerror(errno));
                return false;
        }

        return true;
}

#include <fcgi_stdio.h>

#endif

static void
report_error(void)
{
        printf("400 Bad Request\r\n"
               "Content-Type: text/plain\r\n"
               "\r\n"
               "Invalid query string\r\n");
}

static bool
handle_query_string(void)
{
        const char *qs = getenv("QUERY_STRING");

        if (qs == NULL)
                return false;

        uint64_t id;

        if (!vsx_id_url_decode_id_part(qs, &id))
                return false;

        fputs("Content-Type: image/png\r\n"
              "\r\n",
              stdout);

        uint8_t *png = vsx_alloc(VSX_GENERATE_QR_PNG_SIZE);

        vsx_generate_qr(id, png);

        fwrite(png, 1, VSX_GENERATE_QR_PNG_SIZE, stdout);

        vsx_free(png);

        return true;
}

static void
run_once(void)
{
        if (!handle_query_string())
                report_error();
}

int
main(int argc, char **argv)
{
#ifdef HAVE_FASTCGI
        char *fastcgi_socket_name = NULL;
        int opt;

        while ((opt = getopt(argc, argv, "u:")) != -1) {
                switch (opt) {
                case 'u':
                        fastcgi_socket_name = optarg;
                        break;

                default:
                        fprintf(stderr,
                                "usage: invite-cgi [-u <unix_socket>]");
                        return EXIT_FAILURE;
                }
        }

        if (fastcgi_socket_name && !open_fastcgi_socket(fastcgi_socket_name))
                return EXIT_FAILURE;

        while (FCGI_Accept() >= 0) {
                run_once();
        }
#else
        run_once();
#endif

        return EXIT_SUCCESS;
}
