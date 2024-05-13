#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

#define PAGE_SIZE 256
#define PAGE_TABLE_SIZE 256
#define TLB_SIZE 10
#define PHYSICAL_MEM_SIZE 65536

int num_addresses;
const char *filename = "addresses.txt";
pthread_mutex_t print_mutex;
int algorithm_choice;

typedef struct {
    int page;
    int frame;
} TLBEntry;

void generateAddresses() {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        printf("Error opening file for writing.\n");
        return;
    }
    srand(time(NULL));
    // Generate a random number of addresses between 1 and 10
    num_addresses = rand() % 1000 + 1;
    for (int i = 0; i < num_addresses; i++) {
        int address = rand() % 65536; // Generate a random virtual address between 0 and 65535
        fprintf(file, "%d\n", address);
    }
    fclose(file);
    printf("Generated %d virtual addresses and wrote them to %s.\n", num_addresses, filename);
    sleep(3);
}

int fifoAlgorithm(int *frame_tracker, int num_frames, int page_number) {
    static int frame_index = 0;
    int frame = frame_index;
    frame_index = (frame_index + 1) % num_frames;
    frame_tracker[frame] = page_number;
    return frame;
}

int optimalAlgorithm(int *frame_tracker, int frame_count, int *page_table, int *logical_address, int page_table_size, int *access_counter, int remaining_addresses) {
    int frame_to_replace = -1;
    int max_distance = -1;
    for (int i = 0; i < frame_count; i++) {
        int page_in_frame = frame_tracker[i];
        bool page_used_in_future = false;
        for (int j = 0; j < remaining_addresses; j++) {
            if (logical_address[j] == page_in_frame) {
                access_counter[page_in_frame] = j;
                page_used_in_future = true;
                break;
            }
        }
        if (!page_used_in_future) {
            frame_to_replace = i;
            break;
        }
        if (access_counter[page_in_frame] > max_distance) {
            max_distance = access_counter[page_in_frame];
            frame_to_replace = i;
        }
    }
    return frame_to_replace;
}

void *processAddress(void *arg) {
    int *logical_address = (int *)arg;
    int offset, page_number;
    int physical_address, value, frame;
    int tlb_hit = 0;
    int access_counter[PHYSICAL_MEM_SIZE / PAGE_SIZE];
    int page_fault = 0;
    int tlb_count = 0;
    int tlb_buffer = 0;
    TLBEntry tlb[TLB_SIZE];
    int page_table[PAGE_TABLE_SIZE];
    int physical_memory[PHYSICAL_MEM_SIZE];
    char buffer[PAGE_SIZE];
    FILE *backing_store = fopen("BACKING_STORE.bin", "rb");
    if (backing_store == NULL) {
        printf("Backing store file not found.\n");
        pthread_exit(NULL);
    }
    for (int i = 0; i < PAGE_TABLE_SIZE; i++) {
        page_table[i] = -1;
    }
    for (int i = 0; i < TLB_SIZE; i++) {
        tlb[i].page = -1;
        tlb[i].frame = -1;
    }
    int frame_tracker[PHYSICAL_MEM_SIZE / PAGE_SIZE];
    for (int i = 0; i < PHYSICAL_MEM_SIZE / PAGE_SIZE; i++) {
        frame_tracker[i] = -1;
    }
    // Read logical addresses
    for (int i = 0; i < num_addresses; i++) {
        page_number = (*logical_address & 0xFF00) >> 8;
        offset = *logical_address & 0x00FF;
        int tlb_hit_index = -1;
        // Check if the page is in the TLB
        for (int j = 0; j < tlb_buffer; j++) {
            if (tlb[j].page == page_number) {
                tlb_hit_index = j;
                break;
            }
        }
        if (tlb_hit_index != -1) {
            // TLB hit
            tlb_hit++;
            frame = tlb[tlb_hit_index].frame;
        } else if (page_table[page_number] != -1) {
            // Page table hit
            frame = page_table[page_number];
        } else {
            // Page fault
            fseek(backing_store, page_number * PAGE_SIZE, SEEK_SET);
            fread(buffer, sizeof(char), PAGE_SIZE, backing_store);
            // Determine the frame to replace based on the chosen algorithm
            switch (algorithm_choice) {
                case 1: // FIFO
                    frame = fifoAlgorithm(frame_tracker, PHYSICAL_MEM_SIZE / PAGE_SIZE, page_number);
                    break;
                case 2: // Optimal
                    frame = optimalAlgorithm(frame_tracker, PHYSICAL_MEM_SIZE / PAGE_SIZE, page_table, logical_address + i, num_addresses - i, access_counter, num_addresses - i);
                    break;
                default:
                    printf("Invalid algorithm choice.\n");
                    pthread_exit(NULL);
            }
            // Update TLB if TLB is not full
            if (tlb_buffer < TLB_SIZE) {
                tlb[tlb_buffer].page = page_number;
                tlb[tlb_buffer].frame = frame;
                tlb_buffer++;
            }
            // Update page table
            page_table[page_number] = frame;
            // Read the page into physical memory
            for (int j = 0; j < PAGE_SIZE; j++) {
                physical_memory[frame * PAGE_SIZE + j] = buffer[j];
            }
            page_fault++;
        }
        // Access the value at the physical address
        physical_address = frame * PAGE_SIZE + offset;
        value = physical_memory[physical_address];
        // Print the logical address, physical address, and value
        pthread_mutex_lock(&print_mutex);
        printf("Logical address: %d\tPhysical address: %d\tValue: %d\n", *logical_address, physical_address, value);
        pthread_mutex_unlock(&print_mutex);
        logical_address++;
    }
    // Print TLB hit rate and page fault rate
    pthread_mutex_lock(&print_mutex);
    printf("TLB hit rate: %.2f%%\n", (float)tlb_hit / num_addresses * 100);
    printf("Page fault rate: %.2f%%\n", (float)page_fault / num_addresses * 100);
    pthread_mutex_unlock(&print_mutex);
    fclose(backing_store);
    pthread_exit(NULL);
}

void on_button_clicked(GtkWidget *widget, gpointer data) {
    int choice = GPOINTER_TO_INT(data);
    algorithm_choice = choice;
    printf("Algorithm choice: %d\n", choice);
}

void on_run_button_clicked(GtkWidget *widget, gpointer data) {
    // Your code to execute when the "Run" button is clicked
    // This could involve getting inputs from text entry fields, running the address generation and processing functions, etc.
    generateAddresses();
    GtkWidget *frame_entry = data; // Assuming data contains a pointer to the frame entry widget
    const gchar *frame_text = gtk_entry_get_text(GTK_ENTRY(frame_entry));
    int frames = atoi(frame_text);
    // Get values from the new entry fields for page number and remaining page number
    GtkWidget *page_entry = gtk_entry_new();
    GtkWidget *remaining_entry = gtk_entry_new();
    const gchar *page_text = gtk_entry_get_text(GTK_ENTRY(page_entry));
    const gchar *remaining_text = gtk_entry_get_text(GTK_ENTRY(remaining_entry));
    int page_number = atoi(page_text);
    int remaining_pages = atoi(remaining_text);
    int addresses[1000];
    pthread_t thread;
    pthread_mutex_init(&print_mutex, NULL);
    algorithm_choice = 2; // Choose the optimal algorithm
    pthread_create(&thread, NULL, processAddress, addresses);
    pthread_join(thread, NULL);
    pthread_mutex_destroy(&print_mutex);
}

void FIFOalgo(int stream[], int framesno, int pagesno) {
    int pf = 0;
    printf("Incoming \t Frame 1 \t Frame 2 \t Frame 3 ");
    int temp[framesno];
    for (int m = 0; m < framesno; m++) {
        temp[m] = -1;
    }
    for (int m = 0; m < pagesno; m++) {
        int s = 0;
        for (int n = 0; n < framesno; n++) {
            if (stream[m] == temp[n]) {
                s++;
                pf--;
            }
        }
        pf++;
        if ((pf <= framesno) && (s == 0)) {
            temp[m] = stream[m];
        } else if (s == 0) {
            temp[(pf - 1) % framesno] = stream[m];
        }
        printf("\n");
        printf("%d\t\t\t", stream[m]);
        for (int n = 0; n < framesno; n++) {
            if (temp[n] != -1)
                printf(" %d\t\t\t", temp[n]);
            else
                printf(" - \t\t\t");
        }
    }
    printf("\nTotal pages Faults:\t%d\n", pf);
}

void Optimalalgo(int stream[], int n1, int n2) {
    // Implementation of the Optimal algorithm
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    // Create main window
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Virtual Memory Manager");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    // Create vertical box container
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    // Create labels for displaying welcome message and developer names
    GtkWidget *welcome_label = gtk_label_new("\t ---------------------------------- \n"
                                             "\t Welcome To Virtual Memory Manager \n"
                                             "\t ---------------------------------- \n"
                                             "\t TO MISS NAUSHEEN SHOAIB \n"
                                             "\t ---------------------------------- \n");
    GtkWidget *developers_label = gtk_label_new("\n\n \t         Developers : \n"
                                                "\t      HUZAIFA GHORI (21K-3602) \n");
    // Pack labels into vbox
    gtk_box_pack_start(GTK_BOX(vbox), welcome_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), developers_label, FALSE, FALSE, 0);
    // Add buttons for choosing page replacement algorithm
    GtkWidget *fifo_button = gtk_button_new_with_label("FIFO");
    g_signal_connect(fifo_button, "clicked", G_CALLBACK(on_button_clicked), GINT_TO_POINTER(1));
    gtk_box_pack_start(GTK_BOX(vbox), fifo_button, TRUE, TRUE, 0);
    GtkWidget *optimal_button = gtk_button_new_with_label("Optimal");
    g_signal_connect(optimal_button, "clicked", G_CALLBACK(on_button_clicked), GINT_TO_POINTER(2));
    gtk_box_pack_start(GTK_BOX(vbox), optimal_button, TRUE, TRUE, 0);
    // Add frame entry widget
    GtkWidget *frame_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(frame_entry), "Enter number of frames");
    gtk_box_pack_start(GTK_BOX(vbox), frame_entry, TRUE, TRUE, 0);
    // Add page entry widget
    GtkWidget *page_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(page_entry), "Enter number of pages");
    gtk_box_pack_start(GTK_BOX(vbox), page_entry, TRUE, TRUE, 0);
    // Add remaining page entry widget
    GtkWidget *remaining_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(remaining_entry), "Enter remaining pages");
    gtk_box_pack_start(GTK_BOX(vbox), remaining_entry, TRUE, TRUE, 0);
    // Add "Run" button
    GtkWidget *run_button = gtk_button_new_with_label("Run");
    g_signal_connect(run_button, "clicked", G_CALLBACK(on_run_button_clicked), frame_entry);
    gtk_box_pack_start(GTK_BOX(vbox), run_button, TRUE, TRUE, 0);
    // Show window and start main loop
    gtk_widget_show_all(window);
    gtk_main();
    FILE *addresses_file = fopen(filename, "r");
    if (addresses_file == NULL){
        printf("Error opening file for reading.\n");
        return 1;
    }
    int addresses[1000];
    int *address_ptr = addresses;
    while (fscanf(addresses_file, "%d", address_ptr++) != EOF) {}
    fclose(addresses_file);
    pthread_t thread;
    pthread_mutex_init(&print_mutex, NULL);
    algorithm_choice = 1;  // Choose the FIFO algorithm
    pthread_create(&thread, NULL, processAddress, addresses);
    pthread_join(thread, NULL);
    pthread_mutex_destroy(&print_mutex);
    sleep(2);
    printf("\n\n\n\n");
    int choice;
    printf(" Page Replacement Algorithm Working frame by frame | Press 1 for FIFO | Press 2 for Optimal Replacement : ");
    scanf("%d", &choice);
    int p;
    printf("\n Enter no of pages : ");
    scanf("%d", &p);
    int frames;
    int stream[p];
    printf("\n Enter for upcoming pages : ");
    for (int i = 0; i < p; i++) {
        scanf("%d", &stream[i]);
    }
    printf("\n Enter no of frames : ");
    scanf("%d", &frames);
    switch (choice) {
        case 1:
            FIFOalgo(stream, frames, p);
            break;
        case 2:
            Optimalalgo(stream, frames, p);
            break;
    }
    return 0;
}

