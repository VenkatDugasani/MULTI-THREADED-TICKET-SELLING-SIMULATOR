#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdlib.h>

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t semArr[10];                          // Semaphores for each seller
const char sellerTypes[10] = "HMMMLLLLLL"; // Seller types
int N, currentTime = -1;

typedef struct customer
{
  int customerID;  // Customer ID, -1 if no buyer yet
  int seatID;      // Seat ID, -1 if no seat yet
  int arrivalTime; // Arrival time (0-59), buyers get these randomly
} customer;

customer buyers[10][100]; // Buyers queue: [seller index][position in queue]
customer seats[10][10];   // Seat chart: [row][seat in row]

// Shared variables for medium seller seat assignment
int row_orders[3][10] = {{1,2,3,4,5,6,7,8,9,10},
  {5, 6, 4, 7, 3, 8, 2, 9, 1, 10},
  {10,9,8,7,6,5,4,3,2,1}}; // Row assignment order for medium tickets
int seat_index[3] = {};                                     // Track the current row for seat assignment
int seat_col[3] = {};                                       // Track the current column for seat assignment
int seller_to_type[10]={0,1,1,1,2,2,2,2,2,2};

// Variables for response time, turnaround time, and throughput
int response_times[10] = {0};  // Sum of response times for each seller
int turnaround_times[10] = {0}; // Sum of turnaround times for each seller
int customers_served[10] = {0}; // Number of customers served by each seller
int customers_turned_away[10] = {0}; // Number of customers turned away by each seller

// Print the seating chart after every sale
void print_seating_chart() {
    int i, j;
    printf("\nCurrent Seating Chart:\n");
    for (i = 0; i < 10; i++) {
        for (j = 0; j < 10; j++) {
            int cid = seats[i][j].customerID;
            if (cid != -1) {
                printf("%c%d%02d ", sellerTypes[cid / N], cid / N, cid % N + 1);
            } else {
                printf("---- ");
            }
        }
        printf("\n");
    }
}

int assign_seat_to_customer(int seller_index, customer *cust)
{
  int type=seller_to_type[seller_index];
  pthread_mutex_lock(&mutex); // Ensure mutual exclusion for seat assignment
  while(1){
    if (seat_col[type] >= 10)
    {
      seat_index[type]++;
      seat_col[type] = 0; // Reset column to 0 when moving to the next row
    }
    if (seat_index[type] >= 10)
    {
      printf("Seller %c%d has no more seats available!\n",sellerTypes[seller_index], seller_index);
      pthread_mutex_unlock(&mutex);
      return 1;
    }
    int row = row_orders[type][seat_index[type]]; // Get the next row in the order
    if(seats[row-1][seat_col[type]].customerID>=0){
      seat_col[type]++;
      continue;
    }
    cust->seatID = (row - 1) * 10 + seat_col[type];    // Assign seat (adjust for 0-based indexing)
    seats[row - 1][seat_col[type]] = *cust;            // Place customer in seat
    printf("Customer %02d assigned seat %d,%d by seller %c%d\n", cust->customerID % N + 1, row, seat_col[type] + 1, sellerTypes[seller_index], seller_index);

    seat_col[type]++;            // Move to the next seat in the same row

    // Print updated seating chart
    print_seating_chart();
    break;
  }
  pthread_mutex_unlock(&mutex); // Release lock after seat assignment
  return 0;
}

int delayWidth[10]={2,3,3,3,4,4,4,4,4,4};
int delayOffset[10]={1,2,2,2,4,4,4,4,4,4};
int ending=0;

void *sell(void *index)
{
  int i = (size_t)index, j = 0, service_time = 0, outOfSeats = 0;

  while(!ending){   // Break if there are no seats remaining or no buyers remaining
    pthread_mutex_lock(&mutex);
    sem_post(semArr+i);   // Signal main thread that you're ready
    pthread_cond_wait(&cond, &mutex); // Wait until woken by main thread
    pthread_mutex_unlock(&mutex);

    if(!outOfSeats){
      for (; j < N; j++)
      {
        customer *cust = &buyers[i][j];

        // Skip customers that have already been assigned a seat
        if (cust->arrivalTime > currentTime || (j>0&&buyers[i][j-1].arrivalTime + service_time > currentTime))
        {
          break;
        }

        // Calculate response time (time customer starts being served - arrival time)
        response_times[i] += currentTime - cust->arrivalTime;

        // Simulate service time (2-4 minutes for medium sellers)
        service_time = rand() % delayWidth[i] + delayOffset[i];
        printf("T=%02d: %c%d serving Customer %02d\n", currentTime,sellerTypes[i], i, cust->customerID % N + 1);

        // Try to assign the customer a seat
        outOfSeats = assign_seat_to_customer(i, cust);

        if (outOfSeats) {
            // If no seats are available, increment customers_turned_away
            customers_turned_away[i]++;
        } else {
            // Calculate turnaround time (time customer finishes being served - arrival time)
            turnaround_times[i] += (currentTime + service_time) - cust->arrivalTime;

            // Increment customers served
            customers_served[i]++;
        }
      }
    }
  }

  pthread_exit(NULL); // Exit the thread
}

void wakeup_all_seller_threads()
{
  int i, j, buyersRemaining, seatsRemaining;
  do
  {
    // Wait for all sellers to signal they are ready
    for (i = 0; i < 10; i++)
    {
      sem_wait(semArr + i);
    }

    pthread_mutex_lock(&mutex);
    currentTime++;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);

    // Check if there are any buyers or seats remaining
    buyersRemaining = 0;
    seatsRemaining = 0;
    for (i = 0; i < 10; i++)
    {
      for (j = 0; j < N; j++)
      {
        if (buyers[i][j].seatID < 0)
        {
          buyersRemaining++;
        }
      }
      for (j = 0; j < 10; j++)
      {
        if (seats[i][j].customerID < 0)
        {
          seatsRemaining++;
        }
      }
    }
  }while(buyersRemaining && seatsRemaining);
  ending=1;
  pthread_cond_broadcast(&cond);
}

int custcmp(const void *c1, const void *c2)
{
  int a1 = ((customer *)c1)->arrivalTime, a2 = ((customer *)c2)->arrivalTime;
  return (a1 > a2) - (a1 < a2);
}

// Modified print_statistics function
void print_statistics() {
    int h_customers_served = 0, m_customers_served = 0, l_customers_served = 0;
    int h_response_time = 0, m_response_time = 0, l_response_time = 0;
    int h_turnaround_time = 0, m_turnaround_time = 0, l_turnaround_time = 0;

    for (int i = 0; i < 10; i++) {
        if (sellerTypes[i] == 'H') {
            h_customers_served += customers_served[i];
            h_response_time += response_times[i];
            h_turnaround_time += turnaround_times[i];
        } else if (sellerTypes[i] == 'M') {
            m_customers_served += customers_served[i];
            m_response_time += response_times[i];
            m_turnaround_time += turnaround_times[i];
        } else if (sellerTypes[i] == 'L') {
            l_customers_served += customers_served[i];
            l_response_time += response_times[i];
            l_turnaround_time += turnaround_times[i];
        }
    }

    printf("\nStatistics:\n");

    if (h_customers_served > 0) {
        float h_avg_resp_time = (float)h_response_time / h_customers_served;
        float h_avg_turnaround_time = (float)h_turnaround_time / h_customers_served;
        float h_throughput = (float)h_customers_served / (currentTime + 1);

        printf("High-priced Sellers (H):\n");
        printf("  Customers Served: %d\n", h_customers_served);
        printf("  Average Response Time: %.2f minutes\n", h_avg_resp_time);
        printf("  Average Turnaround Time: %.2f minutes\n", h_avg_turnaround_time);
        printf("  Throughput: %.2f customers/minute\n", h_throughput);
    }

    if (m_customers_served > 0) {
        float m_avg_resp_time = (float)m_response_time / m_customers_served;
        float m_avg_turnaround_time = (float)m_turnaround_time / m_customers_served;
        float m_throughput = (float)m_customers_served / (currentTime + 1);

        printf("Medium-priced Sellers (M):\n");
        printf("  Customers Served: %d\n", m_customers_served);
        printf("  Average Response Time: %.2f minutes\n", m_avg_resp_time);
        printf("  Average Turnaround Time: %.2f minutes\n", m_avg_turnaround_time);
        printf("  Throughput: %.2f customers/minute\n", m_throughput);
    }

    if (l_customers_served > 0) {
        float l_avg_resp_time = (float)l_response_time / l_customers_served;
        float l_avg_turnaround_time = (float)l_turnaround_time / l_customers_served;
        float l_throughput = (float)l_customers_served / (currentTime + 1);

        printf("Low-priced Sellers (L):\n");
        printf("  Customers Served: %d\n", l_customers_served);
        printf("  Average Response Time: %.2f minutes\n", l_avg_resp_time);
        printf("  Average Turnaround Time: %.2f minutes\n", l_avg_turnaround_time);
        printf("  Throughput: %.2f customers/minute\n", l_throughput);
    }
}

int main(int argc, char *argv[])
{
  int i, j;
  pthread_t tids[10];

  if (argc == 2)
  {
    N = atoi(argv[1]); // Number of customers per seller
  }
  else
  {
    printf("Usage: %s <N>\n", argv[0]);
    return 1;
  }

  // Initialize buyers and seating chart
  for(i = 0; i < 10; i++){
    for(j = 0; j < N; j++){
      buyers[i][j]=(customer){i*N+j, -1, rand() % 60};  // Buyer has not yet been sold a seat
    }
    qsort(buyers[i], N, sizeof(customer), custcmp);  // Sort buyers by arrival time
    for(j = 0; j < N; j++){
      buyers[i][j].customerID = i * N + j;
    }
    for(j = N; j < 100; j++){
      buyers[i][j]=(customer){-1, -1, -1};   // No such buyer
    }
  }

  for(i = 0; i < 10; i++){
    for(j = 0; j < 10; j++){
      seats[i][j]=(customer){-1, i * 10 + j, -1};  // Seat is not yet sold to any customer
    }
  }

  // Create seller threads
  for (i = 0; i < 10; i++)
  {
    sem_init(semArr + i, 0, 0); // Initialize semaphore for each seller
    pthread_create(&tids[i], NULL, sell, (void *)(size_t)i);
  }

  // Wake up seller threads and simulate ticket selling
  wakeup_all_seller_threads();

  // Wait for all seller threads to finish
  for (i = 0; i < 10; i++)
  {
    pthread_join(tids[i], NULL);
  }

  // Print the final seating chart
  printf("Final Seat Chart:\n");
  for (i = 0; i < 10; i++) {
    for (j = 0; j < 10; j++) {
      int cid = seats[i][j].customerID;
      if (cid != -1) {
        printf("%c%d%02d ", sellerTypes[cid / N], cid / N, cid % N + 1);
      } else {
        printf("---- ");
      }
    }
    printf("\n");
  }

  // Print statistics for response time, turnaround time, and throughput
  print_statistics();

  return 0;
}
