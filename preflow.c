#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "pthread_barrier.h" 
#include <stdatomic.h> 



#define PRINT 0/* enable/disable prints. */
#define NBROFTHREADS 8
#define SIZE 256

#if PRINT
#define pr(...)		do { fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define pr(...)		/* no effect at all */
#endif

#define MIN(a,b)	(((a)<=(b))?(a):(b))


typedef struct graph_t graph_t;
typedef struct node_adj_edges node_adj_edges;
typedef struct edge_t edge_t;

typedef struct xedge_t	xedge_t;


// Renamed 'task' to 'task_t' for clarit

// Corrected thread_data_t definition

struct edge_t {
    int u;
    int v;
    int f;     /* flow > 0 if from u to v. */
    int c;     /* capacity. */
};

struct node_adj_edges {
  int c;
  int i; 
  int *edges;
};

struct xedge_t {
	int		u;	/* one of the two nodes.	*/
	int		v;	/* the other. 			*/
	int		c;	/* capacity.			*/
};
 
typedef struct {
	int u; //which node
	int operation; // 1 push 0 relabel
  int c_e; //Which edge
} task_t;

typedef struct {
	int i;
	int c;
	task_t *tasks;
} task_list_t;


struct graph_t {
    edge_t *edges;
    int n;          /* nodes. */
    int m;          /* edges. */
    node_adj_edges *adj_edges; //at the node's index a int array of which edges they are connected with
    int *excess;
    int *height;
    int nbr_threads;
	  task_list_t *list;
};


typedef struct {
    graph_t *g;
    int start_node;
    int end_node;
    int id;
} thread_data_t;


#ifdef MAIN
static graph_t* new_graph(FILE* in, int n, int m);
#else
static graph_t* new_graph(int n, int m, int s, int t, xedge_t* e);
#endif
pthread_barrier_t barrier;

static char *progname;

void error(const char *fmt, ...)
{
  va_list ap;
  char buf[BUFSIZ];

  va_start(ap, fmt);
  vsprintf(buf, fmt, ap);

  if (progname != NULL)
    fprintf(stderr, "%s: ", progname);

  fprintf(stderr, "error: %s\n", buf);
  exit(1);
}

static int next_int()
{
  int x;
  int c;

  x = 0;
  while (isdigit(c = getchar()))
    x = 10 * x + c - '0';

  return x;
}

static void *xmalloc(size_t s)
{
  
  void *p;
  p = malloc(s);

  if (p == NULL)
    error("out of memory: malloc(%zu) failed", s);

  return p;
}

static void *xcalloc(size_t n, size_t s)
{
  void *p;

  p = xmalloc(n * s);
  memset(p, 0, n * s);

  return p;
}


static void init_node_adj_edges(node_adj_edges *node, int initial_capacity) {
    node->edges = xmalloc(initial_capacity * sizeof(int));
    node->i = 0;             // Initialize current edge count to 0
    node->c = initial_capacity; // Set initial capacity
}

void init_task_list(graph_t *g, int initial_capacity) {
    g->list = xmalloc(g->n * sizeof(task_list_t));
    for (int i = 0; i < g->n; i++) {
        g->list[i].tasks = xmalloc(initial_capacity * sizeof(task_t));
        g->list[i].c = initial_capacity; 
        g->list[i].i = 0;                
    }
}


static void push(graph_t *g, int u, int v, int edge){
  int		d;	
  pr("u: %d\nv: %d\n",u,v);
  graph_t *graph = g;
  edge_t e = graph->edges[edge];
  if(u == v) return;
	pr("Push from %d to %d: ", u, v);
	pr("f = %d, c = %d, so ", e.f, e.c);

	if (u == e.u) {
		d = MIN(graph->excess[u], e.c - e.f);
		graph->edges[edge].f += d;
    }
	 else {
		d = MIN(graph->excess[u], e.c + e.f);
		graph->edges[edge].f -= d;
  }
	
	pr("pushing %d\n", d);
  g->excess[u] -=d;
  g->excess[v] +=d;

}

static int other(int u, int edge, edge_t *edge_list)
{
  if (u == edge_list[edge].u)
    return edge_list[edge].v;
  else
    return edge_list[edge].u;
}


static void add_edge_to_node(node_adj_edges *node, int edge_index){
    if (node->i == node->c) {
        node->c = node->c * 2; 
        int *new_edge = realloc(node->edges, node->c * sizeof(int));        
        if (new_edge == NULL) {
            error("no memory"); 
        }
        node->edges = new_edge;
    }
    node->edges[node->i] = edge_index; 
    node->i++; 
}

static void add_task(int id, int operation, int u, int c_e, graph_t *g){
	if (g->list[id].i == g->list[id].c) {
        g->list[id].c *= 2;
        task_t *new_tasks = realloc(g->list[id].tasks, g->list[id].c * sizeof(task_t));
        if (new_tasks == NULL) {
            error("no memory"); 
        }
        g->list[id].tasks = new_tasks;
    }
    task_t t;
    t.u = u;
    t.c_e = c_e;
    t.operation = operation;
    g->list[id].tasks[g->list[id].i] = t;
    g->list[id].i++;
}



void *work(void *arg) {
  thread_data_t *data = (thread_data_t *)arg;
  graph_t *g = data->g;
  edge_t *current_edge;
  node_adj_edges *i_adj_edges;

  int *excess = g->excess;
  int start_node = data->start_node;
  int stop_node = data->end_node;

  int b, reLabel, c_e, v, u, j;
  
  while(-excess[0] != excess[g->n-1]){
      
      
      for(u = start_node; u<= stop_node; u += 1){
        reLabel = 1;
        if(g->excess[u] > 0 && u != g->n-1 && u != 0){
          i_adj_edges = &(g->adj_edges[u]);
          for(j = 0; j < i_adj_edges->i; j++){
            current_edge = &g->edges[i_adj_edges->edges[j]];       
            if (u == current_edge->u) {
                  v = current_edge->v;
                  b = 1;
                }
            else {
                  v = current_edge->u;
                  b = -1; 
                }
            if(g->height[u] > g->height[v] && b * current_edge->f < current_edge->c){
              	add_task(data->id, 1, u, i_adj_edges->edges[j], g);
                reLabel = 0;
                break;  
            }
          }
        if(reLabel){
          add_task(data->id, 0, u, i_adj_edges->edges[j], g);
        }
      }
    }

	pthread_barrier_wait(&barrier);
	if(data->id==0){
		for(int i = 0;i< g->nbr_threads; i++){
			for(int j = 0; j<g->list[i].i; j++){
				u = g->list[i].tasks[j].u;
				c_e = g->list[i].tasks[j].c_e;
				if(g->list[i].tasks[j].operation == 1){
					push(g, u, other(u, c_e, g->edges), c_e);
				}
				else{
					g->height[u]++;
				}
			}
      g->list[i].i = 0;
		}
	}
  pthread_barrier_wait(&barrier); 
  }
  return NULL;
} 

int xpreflow(graph_t *g)
{ 
  pr("came to preflow");
  pthread_t thread[NBROFTHREADS];
  thread_data_t data[NBROFTHREADS];
  int status;
  int sourceFlow =0;
  int current_edge;
  node_adj_edges source = g->adj_edges[0];

  for(int i = 0; i < source.i; i += 1)
  {
    current_edge = source.edges[i];
    g->excess[0] += (g->edges)[current_edge].c;
    sourceFlow += (g->edges)[current_edge].c;
    push(g, 0, other(0, current_edge, g->edges), current_edge);
  }

  g->excess[0] -= sourceFlow;
  pr("this is source first:  %d\n", sourceFlow);
  int nodes_per_thread = g->n / NBROFTHREADS; 
  int remainder = g->n % NBROFTHREADS;
  int num_threads = (g->n < NBROFTHREADS) ? g->n : NBROFTHREADS;

  pthread_barrier_init(&barrier, NULL, num_threads);

  for (int i = 0; i < num_threads; i++) {
    int start = i * nodes_per_thread + (i < remainder ? i : remainder); 
    int end = start + nodes_per_thread + (i < remainder ? 1 : 0) - 1;    
    data[i].id = i;
    data[i].g = g;
    data[i].start_node = start;
    data[i].end_node = end;
    g->nbr_threads++;
    status = pthread_create(&thread[i], NULL, work, (void *)&data[i]);
  }

  for (int i = 0; i < num_threads; i++) {
      status = pthread_join(thread[i], NULL);
  }
  
  pthread_barrier_destroy(&barrier);
  return g->excess[g->n-1];
}

static void free_graph(graph_t* g);

int preflow(int n, int m, int s, int t, xedge_t* e)
{
	graph_t*	g;
	int		f;
	double		begin;
	double		end;


	#ifdef MAIN
	#else
	g = new_graph(n, m, s, t, e);
	#endif 
	f = xpreflow(g);
	free_graph(g);

	return f;
}

static void free_graph(graph_t *g) {
    for (int i = 0; i < g->n; i++) {
        free(g->adj_edges[i].edges);  
    }
    
    free(g->adj_edges);

    free(g->edges);

    free(g->excess);

    free(g->height);

    for (int i = 0; i < g->n; i++) {
        free(g->list[i].tasks);
    }
    
    free(g->list);

    free(g);
}


#ifdef MAIN


static graph_t *new_graph(FILE *in, int n, int m)
{
  graph_t *g;
  int i;
  int a;
  int b;
  int c;
  g = xmalloc(sizeof(graph_t));
  g->n = n;
  g->nbr_threads=0;
  g->m = m;
  g->edges = xcalloc(m, sizeof(edge_t));
  g->height = xmalloc(n * sizeof(int));
  g->excess =  xmalloc(n * sizeof(int));
  g->adj_edges = xmalloc(n * sizeof(node_adj_edges));
  g->height[0] = n;

  init_task_list(g, 8);


    
  for (int i = 0; i < n; i++) {
        init_node_adj_edges(&g->adj_edges[i], 8);
    }


  for (i = 0; i < m; i += 1)
  {
    a = next_int();
    b = next_int();
    c = next_int();

    (g->edges + i)->c = c;
    (g->edges + i)->u = a;
    (g->edges + i)->v = b;

    add_edge_to_node(&g->adj_edges[a], i);
    add_edge_to_node(&g->adj_edges[b], i);
  }
  return g;
}

int main(int argc, char *argv[])
{
  pr("program sgtarted");
  FILE *in;   /* input file set to stdin	*/
  graph_t *g; /*undirected graph. 		*/
  int f;      /* output from preflow.		*/
  int n;      /* number of nodes.		*/
  int m;      /* number of edges.		*/
  progname = argv[0]; /* name is a string in argv[0]. */
  in = stdin; /* same as System.in in Java.	*/
  n = next_int();
  m = next_int();
  /* skip C and P from the 6railwayplanning lab in EDAF05 */
  next_int();
  next_int();
  g = new_graph(in, n, m);
  fclose(in);
  f = xpreflow(g);
  printf("f = %d\n",f);
  free_graph(g);
  return 0;
}

#else 
static graph_t *new_graph(int n, int m, int s, int t, xedge_t *e)
{
	
  
  graph_t *g;
  int i;
  int a;
  int b;
  int c;
  g = xmalloc(sizeof(graph_t));
  g->n = n;
  g->nbr_threads=0;
  g->m = m;
  g->edges = xcalloc(m, sizeof(edge_t));
  g->height = xmalloc(n * sizeof(int));
  g->excess =  xmalloc(n * sizeof(int));
  g->adj_edges = xmalloc(n * sizeof(node_adj_edges));
  g->height[0] = n;

  init_task_list(g, SIZE);


  for (int i = 0; i < n; i++) {
        init_node_adj_edges(&g->adj_edges[i], SIZE);
    }

  
  for (i = 0; i < m; i += 1)
  {
   
        a = e[i].u;
        b = e[i].v;
        c = e[i].c;
    

    (g->edges + i)->c = c;
    (g->edges + i)->u = a;
    (g->edges + i)->v = b;

    add_edge_to_node(&g->adj_edges[a], i);
    add_edge_to_node(&g->adj_edges[a], i);
  }
	return g;
}
#endif
