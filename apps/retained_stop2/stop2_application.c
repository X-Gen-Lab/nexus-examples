/**
 * \file            stop2_application.c
 * \brief           Explicit stopped-RUN, retained-mask and restart sequence
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-10-30
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include "stop2_application.h"
#include "nexus_bindings.h"

/** \brief Retain the first actual failure, but BUSY alone is not failure. */
static nx_result_t observed(example_stop2_application_t* app,
                            nx_result_t result) {
    if (app->first_error == NX_SUCCESS && result != NX_SUCCESS &&
        result != NX_ERROR_BUSY) {
        app->first_error = result;
    }
    return result;
}

/** \brief A physical transition result retains its original primary failure.
 */
static void transition_observed(example_stop2_application_t* app) {
    if (app->first_error == NX_SUCCESS) {
        app->first_error = app->transition.primary != NX_SUCCESS
                               ? app->transition.primary
                               : app->transition.cleanup;
    }
    app->phase = app->transition.remaining_effects != 0U ? EXAMPLE_STOP2_RECOVER
                                                         : EXAMPLE_STOP2_REOPEN;
}

/** \brief One lifecycle step; caller retains storage through unfinished work.
 */
nx_result_t example_stop2_progress(example_stop2_application_t* app) {
    if (app == NULL || app->domain == NULL ||
        (unsigned)app->phase > (unsigned)EXAMPLE_STOP2_DONE) {
        return NX_ERROR_INVALID;
    }
    nx_result_t result;
    switch (app->phase) {
        case EXAMPLE_STOP2_CLOSE:
            result = nx_clock_domain_close(app->domain, &app->barrier);
            if (result != NX_SUCCESS) {
                return observed(app, result);
            }
            app->phase = EXAMPLE_STOP2_STOP;
            break;
        case EXAMPLE_STOP2_STOP:
        case EXAMPLE_STOP2_FINAL_STOP:
            result = nx_platform_stop();
            if (result != NX_SUCCESS) {
                return observed(app, result);
            }
            app->phase = app->phase == EXAMPLE_STOP2_STOP
                             ? EXAMPLE_STOP2_RELEASE
                             : EXAMPLE_STOP2_FINAL_RELEASE;
            break;
        case EXAMPLE_STOP2_RELEASE:
        case EXAMPLE_STOP2_FINAL_RELEASE:
            result = nx_clock_domain_release(app->domain, &app->platform_lease);
            if (result != NX_SUCCESS) {
                return observed(app, result);
            }
            if (app->phase == EXAMPLE_STOP2_FINAL_RELEASE) {
                app->phase = EXAMPLE_STOP2_DONE;
                return app->first_error;
            }
            app->phase = EXAMPLE_STOP2_QUIESCENT;
            break;
        case EXAMPLE_STOP2_QUIESCENT:
            result = nx_clock_domain_quiescent(app->barrier);
            if (result != NX_SUCCESS) {
                return observed(app, result);
            }
            app->phase = EXAMPLE_STOP2_ENTER;
            break;
        case EXAMPLE_STOP2_ENTER:
            if (app->first_error != NX_SUCCESS) {
                app->phase = EXAMPLE_STOP2_DONE;
                return app->first_error;
            }
            app->saved_mask = nx_arch_irq_save();
            app->mask_retained = true;
            app->transition =
                nx_stm32u575_stop2_enter(app->barrier, 2048U, 10000U);
            transition_observed(app);
            break;
        case EXAMPLE_STOP2_RECOVER:
            app->transition = nx_stm32u575_stop2_recover(app->barrier, 10000U);
            transition_observed(app);
            break;
        case EXAMPLE_STOP2_REOPEN:
            if (app->mask_retained) {
                nx_arch_irq_restore(app->saved_mask);
                app->mask_retained = false;
            }
            if (app->first_error != NX_SUCCESS) {
                app->phase = EXAMPLE_STOP2_DONE;
                return app->first_error;
            }
            result = nx_clock_domain_reopen(app->barrier);
            if (result != NX_SUCCESS) {
                app->first_error = result;
                app->phase = EXAMPLE_STOP2_DONE;
                return result;
            }
            app->phase = EXAMPLE_STOP2_ACQUIRE;
            break;
        case EXAMPLE_STOP2_ACQUIRE:
            result = nx_clock_domain_acquire(app->domain, &app->platform_lease);
            if (result != NX_SUCCESS) {
                app->first_error = result;
                app->phase = EXAMPLE_STOP2_DONE;
                return result;
            }
            app->phase = EXAMPLE_STOP2_RESTART;
            break;
        case EXAMPLE_STOP2_RESTART: {
            nx_platform_start_result_t started = nx_platform_start();
            result = started.primary != NX_SUCCESS     ? started.primary
                     : started.cleanup != NX_SUCCESS   ? started.cleanup
                     : started.remaining_effects != 0U ? NX_ERROR_IO
                                                       : NX_SUCCESS;
            /* BUSY here rejects startup; it is not asynchronous stop progress.
             */
            if (app->first_error == NX_SUCCESS) {
                app->first_error = result;
            }
            app->restarted = result == NX_SUCCESS;
            /* Partial startup still owns its RUN lease until actual stop. */
            app->phase = EXAMPLE_STOP2_FINAL_STOP;
            break;
        }
        case EXAMPLE_STOP2_DONE:
            return app->first_error;
    }
    return NX_ERROR_BUSY;
}
