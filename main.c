#include "poppler-document.h"
#include "poppler-page.h"
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <gtk/gtkstatusbar.h>
#include <gtkosxapplication.h>
#include <poppler.h>
#include <signal.h>
#include <stdio.h>
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
static void on_destroy(GtkWidget *widget, gpointer data);
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer data);
static gboolean on_signal_reload(GIOChannel *source, GIOCondition condition,
                                 gpointer data);

static gboolean on_signal_jump(GIOChannel *source, GIOCondition condition,
                               gpointer data);

static int current_page = 0;
static GtkWidget *drawing_area;
static GtkWidget *status_bar;
static PopplerDocument *document;
static GList *occurencies;
static char *file_path = NULL;

static float ZOOM_FACTOR = 1.1;
static int DARK_MODE = 1;

static guint context_id;
static char message[4096];

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // Check for --reload argument
    if (argc > 1 && g_strcmp0(argv[1], "--reload") == 0) {
        pid_t pid;
        int fd = open("/tmp/pdf-viewer/id.pid", O_RDONLY);
        if (fd != -1) {
            FILE *file = fdopen(fd, "r");
            if (file != NULL) {
                fscanf(file, "%d", &pid);
                fclose(file);
                kill(pid, SIGUSR1);
            }
            close(fd);
        }
        return 0;
    }

    // Check for file path argument
    if (argc > 1) {
        file_path = argv[1];
    } else {
        g_error("No file path provided.");
        return 1;
    }

    // Check if file exists
    if (access(file_path, F_OK) == -1) {
        file_path = "/tmp/pdf-viewer/failback.pdf";
        FILE *empty_pdf = fopen(file_path, "w");
        if (empty_pdf != NULL) {
            const char *empty_pdf_content =
                "%PDF-1.7\n"
                "1 0 obj\n"
                "<< /Type /Catalog /Pages 2 0 R >>\n"
                "endobj\n"
                "2 0 obj\n"
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
                "endobj\n"
                "3 0 obj\n"
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>\n"
                "endobj\n"
                "xref\n"
                "0 4\n"
                "0000000000 65535 f \n"
                "0000000010 00000 n \n"
                "0000000053 00000 n \n"
                "0000000115 00000 n \n"
                "trailer\n"
                "<< /Size 4 /Root 1 0 R >>\n"
                "startxref\n"
                "149\n"
                "%%EOF\n";
            fwrite(empty_pdf_content, 1, strlen(empty_pdf_content), empty_pdf);
            fclose(empty_pdf);

            int fifo_fd =
                open("/tmp/pdf-viewer/req.file", O_WRONLY | O_CREAT, 0666);
            if (fifo_fd != -1) {
                write(fifo_fd, file_path, strlen(file_path) + 1);
                close(fifo_fd);
            }
        } else {
            g_error("Could not create empty PDF file.");
            return 1;
        }
    }

    // Check for --reuse-window argument
    if (argc > 2 && g_strcmp0(argv[2], "--reuse-window") == 0) {
        current_page = 0;
        int fifo_fd =
            open("/tmp/pdf-viewer/req.file", O_WRONLY | O_CREAT, 0666);
        if (fifo_fd != -1) {
            write(fifo_fd, file_path, strlen(file_path) + 1);
            close(fifo_fd);

            pid_t pid;
            int pid_fd = open("/tmp/pdf-viewer/id.pid", O_RDONLY);
            if (pid_fd != -1) {
                FILE *pid_file = fdopen(pid_fd, "r");
                if (pid_file != NULL) {
                    fscanf(pid_file, "%d", &pid);
                    fclose(pid_file);
                    if (kill(pid, 0) == -1 && errno == ESRCH) {
                        printf("Hello Bug!");
                        goto proceed_main;
                    } else {
                        kill(pid, SIGUSR1);
                    }
                }
                close(pid_fd);
            }
            return 0;
        } else {
            g_error("Could not open FIFO for writing.");
            return 1;
        }
    }

    // Check for --jump-to argument
    if (argc > 2 && g_strcmp0(argv[2], "--jump-to") == 0) {
        if (argc > 3) {
            char *jump_to_arg = argv[3];

            int fifo_fd = open("/tmp/pdf-viewer/jump-to.string",
                               O_WRONLY | O_CREAT, 0666);
            if (fifo_fd != -1) {
                write(fifo_fd, jump_to_arg, strlen(jump_to_arg) + 1);
                close(fifo_fd);

                pid_t pid;
                int pid_fd = open("/tmp/pdf-viewer/id.pid", O_RDONLY);
                if (pid_fd != -1) {
                    FILE *pid_file = fdopen(pid_fd, "r");
                    if (pid_file != NULL) {
                        fscanf(pid_file, "%d", &pid);
                        fclose(pid_file);
                        kill(pid, SIGUSR2);
                    }
                    close(pid_fd);
                }
                return 0;
            } else {
                g_error("Could not open FIFO for writing.");
                return 1;
            }
        } else {
            g_error("No argument provided for --jump-to.");
            return 1;
        }
    }

proceed_main:;

    char *uri = g_filename_to_uri(file_path, NULL, NULL);
    document = poppler_document_new_from_file(uri, NULL, NULL);
    g_free(uri);
    if (!document) {
        g_error("Error opening PDF file.");
        return 1;
    }

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 1260);

    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), document);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    drawing_area = gtk_drawing_area_new();
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw), document);

    status_bar = gtk_statusbar_new();

    /*
    GtkWidget *label1 = gtk_label_new("Text 1");
    GtkWidget *label2 = gtk_label_new("Text 2");

    gtk_box_pack_start(GTK_BOX(status_bar), label1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(status_bar), label2, FALSE, FALSE, 0);
    */

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), status_bar, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(window), vbox);

    gtk_widget_show_all(window);

    GtkosxApplication *theApp = g_object_new(GTKOSX_TYPE_APPLICATION, NULL);
    gtkosx_application_set_menu_bar(theApp, GTK_MENU_SHELL(gtk_menu_bar_new()));
    gtkosx_application_ready(theApp);

    int fd = open("/tmp/pdf-viewer/id.pid", O_WRONLY | O_CREAT, 0666);
    if (fd != -1) {
        dprintf(fd, "%d", getpid());
        close(fd);
    }

    signal(SIGUSR1, SIG_IGN);
    int kq_reload = kqueue();
    struct kevent kev_reload;
    EV_SET(&kev_reload, SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    kevent(kq_reload, &kev_reload, 1, NULL, 0, NULL);
    GIOChannel *channel_reload = g_io_channel_unix_new(kq_reload);
    g_io_add_watch(channel_reload, G_IO_IN, on_signal_reload, NULL);

    signal(SIGUSR2, SIG_IGN);
    int kq_jump = kqueue();
    struct kevent kev_jump;
    EV_SET(&kev_jump, SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
    kevent(kq_jump, &kev_jump, 1, NULL, 0, NULL);
    GIOChannel *channel_jump = g_io_channel_unix_new(kq_jump);
    g_io_add_watch(channel_jump, G_IO_IN, on_signal_jump, NULL);

    gtk_main();

    return 0;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    PopplerPage *page = poppler_document_get_page(document, current_page);
    double pdf_width, pdf_height;
    int all_pages = poppler_document_get_n_pages(document);
    poppler_page_get_size(page, &pdf_width, &pdf_height);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    double scale =
        MIN(allocation.width / pdf_width, allocation.height / pdf_height);

    scale = scale * ZOOM_FACTOR;
    cairo_translate(cr, (allocation.width - pdf_width * scale) / 2,
                    (allocation.height - pdf_height * scale) / 2);
    cairo_scale(cr, scale, scale);

    context_id =
        gtk_statusbar_get_context_id(GTK_STATUSBAR(status_bar), "Example");

    sprintf(message, "Page %d / %d [ %s ]", current_page + 1, all_pages,
            "Label");
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), context_id, message);

    poppler_page_render(page, cr);

    if (occurencies) {
        PopplerColor redColor = {65535, 0, 0};

        for (GList *l = occurencies; l != NULL; l = l->next) {
            PopplerRectangle *rectangle = (PopplerRectangle *)l->data;
            poppler_page_render_selection(page, cr, rectangle, NULL,
                                          POPPLER_SELECTION_GLYPH, &redColor,
                                          &redColor);
        }
    }

    if (DARK_MODE) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }

    return FALSE;
}

static void on_destroy(GtkWidget *widget, gpointer data) {
    g_object_unref(data);
    gtk_main_quit();
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer data) {
    int num_pages = poppler_document_get_n_pages(document);
    switch (event->keyval) {
    case GDK_KEY_j:
        if (current_page < num_pages - 1) {
            current_page++;
            gtk_widget_queue_draw(drawing_area);
        }
        break;
    case GDK_KEY_k:
        if (current_page > 0) {
            current_page--;
            gtk_widget_queue_draw(drawing_area);
        }
        break;
    case GDK_KEY_plus:
        ZOOM_FACTOR = ZOOM_FACTOR + 0.1;
        gtk_widget_queue_draw(drawing_area);
        break;
    case GDK_KEY_minus:
        ZOOM_FACTOR = ZOOM_FACTOR - 0.1;
        gtk_widget_queue_draw(drawing_area);
        break;
    case GDK_KEY_r:
        ZOOM_FACTOR = 1.1;
        gtk_widget_queue_draw(drawing_area);
        break;
    case GDK_KEY_d:
        DARK_MODE = !DARK_MODE;
        gtk_widget_queue_draw(drawing_area);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

static gboolean on_signal_reload(GIOChannel *source, GIOCondition condition,
                                 gpointer data) {
    if (condition & G_IO_IN) {
        struct kevent kev;
        if (kevent(g_io_channel_unix_get_fd(source), NULL, 0, &kev, 1, NULL) >
            0) {
            if (kev.filter == EVFILT_SIGNAL) {
                int fifo_fd =
                    open("/tmp/pdf-viewer/req.file", O_RDONLY | O_CREAT, 0666);
                if (fifo_fd != -1) {
                    char new_file_path[4096];
                    ssize_t num_read =
                        read(fifo_fd, new_file_path, sizeof(new_file_path));
                    if (num_read > 0) {
                        new_file_path[num_read] = '\0';
                        file_path = new_file_path;
                    }
                    close(fifo_fd);
                }

                if (document) {
                    g_object_unref(document);
                }
                char *uri = g_filename_to_uri(file_path, NULL, NULL);
                document = poppler_document_new_from_file(uri, NULL, NULL);
                g_free(uri);
                if (!document) {
                    g_error("Error reloading PDF file.");
                    return FALSE;
                }
                gtk_widget_queue_draw(drawing_area);
            }
        }
    }
    return TRUE;
}

static gboolean on_signal_jump(GIOChannel *source, GIOCondition condition,
                               gpointer data) {
    if (condition & G_IO_IN) {
        struct kevent kev;
        if (kevent(g_io_channel_unix_get_fd(source), NULL, 0, &kev, 1, NULL) >
            0) {
            if (kev.filter == EVFILT_SIGNAL) {
                int fifo_fd = open("/tmp/pdf-viewer/jump-to.string",
                                   O_RDONLY | O_NONBLOCK);
                if (fifo_fd != -1) {
                    char jump_to_arg[4096];
                    ssize_t num_read =
                        read(fifo_fd, jump_to_arg, sizeof(jump_to_arg));
                    if (num_read > 0) {
                        jump_to_arg[num_read] = '\0';
                        for (int i = 0;
                             i < poppler_document_get_n_pages(document); i++) {
                            PopplerPage *page =
                                poppler_document_get_page(document, i);

                            occurencies = poppler_page_find_text_with_options(
                                page, jump_to_arg, POPPLER_FIND_MULTILINE);

                            if (occurencies) {
                                current_page = i;
                                gtk_widget_queue_draw(drawing_area);
                            }
                        }
                    }
                    close(fifo_fd);
                }
            }
        }
    }
    return TRUE;
}
