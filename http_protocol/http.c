#include "http.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <dc/unistd.h>
#include <dc/stdlib.h>

#define CRLF "\r\n"
#define BODY_BUFFER 256

static void parse_request_header(char * raw_header, http_request * request);
static int parse_request_method(char * method);
static char * substring(const char * string, size_t start, size_t end);
static int parse_uri_to_filepath(config * conf, char * request_uri, char ** request_path);
static char * get_status_phrase(int status_code);
static char * get_utc_time();

void http_handle_client(config * conf, int cfd) {
    char request_buf[MAX_REQUEST_LEN];
    memset(request_buf, 0, MAX_REQUEST_LEN); // You will regret removing this line
    
    ssize_t num_read = read(cfd, request_buf, MAX_REQUEST_LEN);

    http_request * request = parse_request(request_buf, num_read);
    http_response * response = build_response(conf, request);
    send_response(response, cfd);

    http_request_destroy(request);
    http_response_destroy(response);
}

http_request * parse_request(char * request_text, size_t request_len) {
    char * end_of_header = strstr(request_text, "\r\n\r\n");
    if (end_of_header == NULL) end_of_header = strstr(request_text, "\n\n");
    if (end_of_header == NULL) return NULL;

    char * end_of_request_line = strstr(request_text, "\r\n");
    if (end_of_request_line == NULL) end_of_request_line = strstr(request_text, "\n");
    if (end_of_request_line == NULL) return NULL;
    
    
    char * request_body = strdup(end_of_header);
    int request_body_len = strlen(end_of_header);

    char * request_header = substring(request_text, 0, request_len - request_body_len);
    http_request * request = malloc(sizeof(http_request));
    parse_request_header(request_header, request);
    request->request_body = request_body;
    free(request_header);

    return request;
}

http_response * build_response(config * conf, http_request * request) {
    str_map * header_fields = sm_create(4);
    sm_put(header_fields, "Server", "DataComm/0.1");
    sm_put(header_fields, "Date", get_utc_time());

    http_response * response = malloc(sizeof(http_response));
    response->header_fields = header_fields;

    if (request == NULL) {
        response->response_code = 400;
        return response;
    }

    response->method = request->method;
    int path_status = parse_uri_to_filepath(conf, request->request_uri, &response->request_path);

    if (path_status == -1) {
        response->response_code = HTTP_SERVER_ERROR;
        return response;
    } else if (path_status == 0) {
        response->response_code = HTTP_NOT_FOUND;
    } else {
        response->response_code = HTTP_OK;
    }

    struct stat st;
    int stat_status = stat(response->request_path, &st);

    if (stat_status) {
        response->response_code = HTTP_SERVER_ERROR;
        return response;
    } else {
        long size = st.st_size;
        char size_buffer[15];
        sprintf(size_buffer, "%ld", size);

        sm_put(header_fields, "Content-Length", size_buffer);
    }

    return response;
}

void send_response(http_response * response, int cfd) {
    const char * version = "HTTP/1.0 ";
    write(cfd, version, strlen(version));
    const char * status_phrase = get_status_phrase(response->response_code);
    write(cfd, status_phrase, strlen(status_phrase));
    write(cfd, CRLF, 2);

    str_map * header_fields = response->header_fields;
    size_t header_lines = sm_size(header_fields);
    char ** header_keys = sm_get_keys(header_fields);
    for (size_t i = 0; i < header_lines; i++) {
        char buf[MAX_HEADER_VALUE_LEN];
        sprintf(buf, "%s: %s", header_keys[i], sm_get(header_fields, header_keys[i]));
        int len = strlen(buf);
        write(cfd, buf, len);
        write(cfd, CRLF, 2);
    }

    write(cfd, CRLF, 2);

    if (response->method == METHOD_HEAD) return;
    if (response->response_code == HTTP_SERVER_ERROR) return;

    const char * content_filepath = response->request_path;
    int content_fd = open(content_filepath, O_RDONLY);

    ssize_t num_read;
    char buf[BODY_BUFFER];
    num_read = read(content_fd, buf, BODY_BUFFER);
    while (num_read > 0) {
        write(cfd, buf, num_read);
        num_read = read(content_fd, buf, BODY_BUFFER);
    }
    close(content_fd);
}

void http_request_destroy(http_request * request) {
    if (request == NULL) return;

    sm_destroy(request->header_fields);
    free(request->http_version);
    free(request->request_uri);
    free(request->request_body);
    free(request);
}

void http_response_destroy(http_response * response) {
    if (response == NULL) return;
    if (response->request_path != NULL)
        free(response->request_path);

    sm_destroy(response->header_fields);
    free(response);
}

static char * substring(const char * string, size_t start, size_t end) {
    size_t out_len = end - start;
    char * out = calloc(out_len + 1, sizeof(char));
    for (size_t i = 0; i < out_len; i++) {
        out[i] = string[start + i];
    }
    out[out_len] = '\0';
    return out;
}

static int parse_request_method(char * method) {
    if (strcmp(method, "GET") == 0) {
        return METHOD_GET;
    } 
    
    if (strcmp(method, "HEAD") == 0) {
        return METHOD_HEAD;
    }

    return METHOD_UNSUPPORTED;
}

static char * get_status_phrase(int status_code) {
    if (status_code == HTTP_OK) {
        return "200 OK";
    }
    
    if (status_code == HTTP_NOT_FOUND) {
        return "404 Not Found";
    }

    if (status_code == HTTP_BAD_REQUEST) {
        return "400 Bad Request";
    }

    return "500 Internal Server Error";
}

static char * get_utc_time() {
    time_t raw;
    struct tm *utc;

    time(&raw);
    utc = gmtime(&raw);

    char * time_text = asctime(utc);
    time_text[24] = '\0';
    return time_text;
}

// Parsing according to example at: https://linux.die.net/man/3/strtok_r
static void parse_request_header(char * raw_header, http_request * request) {
    char * saveptr1, * saveptr2, * saveptr3;
    char * request_line = strtok_r(raw_header, "\r\n", &saveptr1);
    
    char * method_str = strtok_r(request_line, " ", &saveptr2);
    char * uri_str = strtok_r(NULL, " ", &saveptr2);
    char * version_str = strtok_r(NULL, " ", &saveptr2);

    str_map * fields_map = sm_create(4);
    char * header_field = strtok_r(NULL, "\r\n", &saveptr1);
    while (header_field != NULL) {
        char * lhs = strtok_r(header_field, ":", &saveptr3);
        char * rhs = strtok_r(NULL, ":", &saveptr3);
        sm_put(fields_map, lhs, rhs);
        header_field = strtok_r(NULL, "\r\n", &saveptr1);
    }

    request->method = parse_request_method(method_str);
    request->http_version = strdup(version_str);
    request->request_uri = strdup(uri_str);
    request->header_fields = fields_map;
}

// Returns 1 if able to open request_uri
// Returns 0 if can't open request_uri but can open not found page
// Returns -1 if can't open either (Server Error)
static int parse_uri_to_filepath(config * conf, char * request_uri, char ** request_path) {
    if (request_uri == NULL) {
        *request_path = NULL;
        return -1;
    }

    // TODO: Get these from settings
    char * serving_directory = conf->root_dir;
    char * not_found_page = conf->not_found_page;
    char * index_page = conf->index_page;

    if (strcmp(request_uri, "/") == 0) {
        request_uri = index_page;
    }

    char filepath_buf[MAX_URI_PATH_LEN];
    memset(filepath_buf, 0, MAX_URI_PATH_LEN);
    sprintf(filepath_buf, "%s%s", serving_directory, request_uri);

    if( access( filepath_buf, F_OK ) != -1 ) {
        size_t filepath_len = strlen(filepath_buf);
        *request_path = calloc(filepath_len + 1, sizeof(char));
        strcpy(*request_path, filepath_buf);
        return 1;
    }

    memset(filepath_buf, 0, MAX_URI_PATH_LEN);
    sprintf(filepath_buf, "%s%s", serving_directory, not_found_page);

    if( access( filepath_buf, F_OK ) != -1 ) {
        size_t filepath_len = strlen(filepath_buf);
        *request_path = calloc(filepath_len + 1, sizeof(char));
        strcpy(*request_path, filepath_buf);
        return 0;
    }

    *request_path = NULL;
    return -1;
}
