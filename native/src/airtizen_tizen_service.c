#include "airtizen_runtime.h"
#include "airtizen_control_http.h"

#include <dlog.h>
#include <service_app.h>
#include <string.h>

#define LOG_TAG "AirTizenService"

static bool on_create(void *data)
{
    int rc;
    (void)data;

    rc = airtizen_control_http_start(45110);
    if (rc != 0) {
        dlog_print(DLOG_ERROR, LOG_TAG, "control HTTP start failed rc=%d", rc);
        return true;
    }

    rc = airtizen_runtime_start("AirTizen TV");
    dlog_print(rc == 0 ? DLOG_INFO : DLOG_ERROR,
               LOG_TAG,
               "AirTizen runtime start rc=%d",
               rc);

    return rc == 0;
}

static void on_control(app_control_h app_control, void *data)
{
    (void)app_control;
    (void)data;

    airtizen_runtime_start("AirTizen TV");
}

static void on_terminate(void *data)
{
    (void)data;

    airtizen_runtime_stop();
    airtizen_control_http_stop();
}

int main(int argc, char *argv[])
{
    service_app_lifecycle_callback_s cb;
    memset(&cb, 0, sizeof(cb));

    cb.create = on_create;
    cb.terminate = on_terminate;
    cb.app_control = on_control;

    return service_app_main(argc, argv, &cb, NULL);
}
