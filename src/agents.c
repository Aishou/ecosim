#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <GLFW/glfw3.h>
#include <time.h>
#include <math.h>
#include "utils.h"
#include "agents.h"
#include "quadtree.h"

#define AGENT_ARRAY_DEFAULT_SIZE (16)
#define AGENT_DEBUG_SHOW_VISION (0)

/* Create empty agent array */
Agent_array*
agent_array_create()
{
  Agent_array* temp = malloc(sizeof(Agent_array));
  temp->count = 0;
  temp->capacity = AGENT_ARRAY_DEFAULT_SIZE;
  temp->size = sizeof(Agent*) * temp->capacity;
  temp->agents = malloc(temp->size);

  temp->count_change = 0;

  return temp;
}

/* To insert agent to agent array */
void
agent_array_insert(Agent_array* aa, Agent* a)
{
  /* resize of needed */
  if(aa->count + 1 >= aa->capacity){
    aa->capacity *= 2;
    aa->size = sizeof(Agent*) * aa->capacity;
    aa->agents = realloc(aa->agents, aa->size);
  }
  /* save the agent to array */
  aa->agents[aa->count] = a;
  aa->count++;
}

/* Create random agent */
Agent*
agent_create_random()
{
  Agent* tmp_agent = malloc(sizeof(Agent));
  /* random DNA */
  tmp_agent->dna.metabolism = RANDF_MIN(AGENT_METAB_MIN, AGENT_METAB_MAX);
  tmp_agent->dna.fear = RANDF_MIN(AGENT_FEAR_MIN, AGENT_FEAR_MAX);
  // color
  tmp_agent->rgb.r = 1 - tmp_agent->dna.fear + 0.5; //RANDF(AGENT_RGB_MAX);
  tmp_agent->rgb.g = 0.0f; //RANDF(AGENT_RGB_MAX);
  tmp_agent->rgb.b = tmp_agent->dna.metabolism * 2.0; //RANDF(AGENT_RGB_MAX);

  tmp_agent->x = RANDF_MIN(WORLD_MIN_COORD, WORLD_MAX_COORD);
  tmp_agent->y = RANDF_MIN(WORLD_MIN_COORD, WORLD_MAX_COORD);
  tmp_agent->velocity.x = RANDF_MIN(AGENT_MIN_VELOCITY, AGENT_MAX_VELOCITY);
  tmp_agent->velocity.y = RANDF_MIN(AGENT_MIN_VELOCITY, AGENT_MAX_VELOCITY);

  // default state
  tmp_agent->state = AGENT_STATE_LIVING;
  tmp_agent->energy = AGENT_ENERGY_DEFAULT;
  //tmp_agent->metabolism = RANDF_MIN(AGENT_METAB_MIN, AGENT_METAB_MAX);

  return tmp_agent;

}

/* For testing: Create an agent array with randomised population*/
Agent_array*
agent_array_setup_random(int count)
{
  int i;
  Agent_array* tmp_aa = agent_array_create();
  Agent* tmp_a = agent_create_random();

  for(i = 0; i < count; i++)
  {
    tmp_a = agent_create_random();
    agent_array_insert(tmp_aa, tmp_a);
  }
  return tmp_aa;
}

void
agent_array_free(Agent_array* aa)
{
  int i;
  /* free each agent */
  for(i = 0; i < aa->count; i++)
    free(aa->agents[i]);

  /* free everything else */
  free(aa->agents);
  free(aa);
}

/* update agents from agent array */
void
agents_update(Agent_array* aa, Quadtree* quad)
{
  int i;
  Agent* a_ptr;
  Agent_array* local_agents;

  for(i = 0; i < aa->count; i++)
  {
    /* temp pointer for agent */
    a_ptr = aa->agents[i];

    // If the agent isn't living, don't update anything
    if(a_ptr->state != AGENT_STATE_LIVING) continue;

    /* get local agents */
    local_agents = agents_get_local(a_ptr, quad, 0.1);
    if(i % 2)
      for(int j = 0; j < local_agents->count; j++){
        agent_update_mv_avoid(a_ptr, local_agents->agents[j]);
      }

    /* updates */
    agents_update_location(a_ptr);

    agents_update_energy(a_ptr);

  }
}

Agent_array*
agents_get_local(Agent* a_ptr, Quadtree* quad, float radius)
{
  int i;
  int ignore;

  Quadtree_query* query = quadtree_query_setup();
  Agent_array* agent_array = agent_array_create();
  Agent* tmp_a;

  float pos[] = {a_ptr->x, a_ptr->y};
  float half_rad = radius * 0.5;

  /* radius sized box around agent */
  float bot_left[] = {
    a_ptr->x - half_rad, a_ptr->y - half_rad
  };
  float top_right[] = {
    a_ptr->x + half_rad, a_ptr->y + half_rad
  };

  /* Do query */
  quadtree_query(quad, query, pos, radius);

  /* debug */
  if(AGENT_DEBUG_SHOW_VISION){
    quadtree_query_dump(query);
    glColor3f(1.0, 0.0, 0.0);
    glBegin(GL_LINE_LOOP);
    glVertex3f(a_ptr->x - half_rad, a_ptr->y - half_rad, 0.0);
    glVertex3f(a_ptr->x + half_rad, a_ptr->y - half_rad, 0.0);
    glVertex3f(a_ptr->x + half_rad, a_ptr->y + half_rad, 0.0);
    glVertex3f(a_ptr->x - half_rad, a_ptr->y + half_rad, 0.0);
    glEnd();

  }
  /* Loop through agents */
  for(i = 0; i < query->ptr_count; i++)
  {
    tmp_a = query->ptrs[i];
    ignore = 0;

    /* Ignore null pointers and self */
    if(tmp_a == NULL)
      ignore = 1;
    if(a_ptr == tmp_a)
      ignore = 1;

    /* Ignore not within Agents line of sight? */
    if(tmp_a->x > top_right[0] ||
        tmp_a->x < bot_left[0]  ||
        tmp_a->y > top_right[1] ||
        tmp_a->y < bot_left[1])
      ignore = 1;

    /* Ignore pruneable agents */
    if(tmp_a->state == AGENT_STATE_PRUNE)
      ignore = 1;

    /* Insert if valid agent */
    if(!ignore) agent_array_insert(agent_array, tmp_a);
  }

  quadtree_query_free(query);
  return agent_array;
}

void
agents_update_location(Agent* a_ptr)
{
  /* Calculate move magnitude */
  float mv_amt = agents_update_mv_amt(a_ptr);

  /* also change velocity into an array */
  /* Move agents */
  a_ptr->x += a_ptr->velocity.x * mv_amt;
  a_ptr->y += a_ptr->velocity.y * mv_amt;

  /* Wrap if needed */
  agents_update_mv_wrap(a_ptr);
}

float
agents_update_mv_amt(Agent* a_ptr)
{
  return AGENT_MAX_SPEED * a_ptr->dna.metabolism;
}

/* This function is stupid.
 * Put X and y for agents into an arrary so this type
 * of thing doesn't exist. */
void
agents_update_mv_wrap(Agent* a_ptr)
{
  float* tmp_ptr;
  /* loop through x and y and put the agents at the opposite side
   * of the screen if they're past the ends */
  for(tmp_ptr = &a_ptr->x; tmp_ptr <= &a_ptr->y; tmp_ptr++){
    *tmp_ptr = ((*tmp_ptr> WORLD_MAX_COORD) || (*tmp_ptr < WORLD_MIN_COORD)) ?
      -(*tmp_ptr) :
      *tmp_ptr;
  }
}

void 
agent_normalize_velocity(Agent* a_ptr)
{
  float mag = sqrt((a_ptr->velocity.x * a_ptr->velocity.x) 
           + (a_ptr->velocity.y * a_ptr->velocity.y));
  a_ptr->velocity.x = a_ptr->velocity.x / mag;
  a_ptr->velocity.y = a_ptr->velocity.y / mag; 

}

void
agent_update_mv_avoid(Agent* a_ptr, Agent* t_ptr)
{
  float diff[] = {0.0f, 0.0f};
  float mag = 0.0f;

  diff[0] = a_ptr->x - t_ptr->x;
  diff[1] = a_ptr->y - t_ptr->y;

  /* Noramlise and set vector */
  mag = sqrt((diff[0] * diff[0]) + (diff[1] * diff[1]));
  a_ptr->velocity.x += (diff[0] / mag) * a_ptr->dna.metabolism * a_ptr->dna.fear;
  a_ptr->velocity.y += (diff[1] / mag) * a_ptr->dna.metabolism * a_ptr->dna.fear;

  agent_normalize_velocity(a_ptr);
}

void
agents_update_energy(Agent* a_ptr)
{
  a_ptr->energy -= AGENT_METAB_ENERGY_SCALE(a_ptr->dna.metabolism) * AGENTS_TIME_FACTOR;
  if(a_ptr->energy < AGENTS_ENERGY_DEAD) a_ptr->state = AGENT_STATE_DEAD;
}

Agent_verts*
agent_verts_create()
{
  Agent_verts* tmp = malloc(sizeof(Agent_verts));
  tmp->capacity = sizeof(float) * (4);
  tmp->size = 0;
  tmp->verts_pos = malloc(tmp->capacity);
  tmp->verts_col = malloc(tmp->capacity);
  tmp->a_count = 0;
  tmp->end = 0;
  return tmp;
}

void
agent_verts_free(Agent_verts* av)
{
  free(av->verts_pos);
  free(av->verts_col);
  free(av);
}

void
agents_to_verts(Agent_array* aa, Agent_verts* av)
{
  int i;
  size_t new_size = (sizeof(float) * 4 * aa->count); //4 floats
  av->end = 0;
  av->a_count= 0;

  /* if verts array too big, grow */
  if(new_size > av->capacity){
    av->capacity = new_size;
    av->verts_pos = realloc(av->verts_pos, av->capacity);
    av->verts_col = realloc(av->verts_col, av->capacity);
  }

  for(i = 0; i < aa->count ; i++) {
    Agent* agent = aa->agents[i];
    float* pos = av->verts_pos;
    float* col = av->verts_col;

    pos[av->end] = agent->x;
    av->verts_col[av->end] = agent->rgb.r;
    av->end++;

    pos[av->end] = agent->y;
    col[av->end] = agent->rgb.g;
    av->end++;

    pos[av->end] = 0.0f;
    col[av->end] = agent->rgb.b;
    av->end++;

    pos[av->end] = AGENT_ENERGY_SIZE_SCALE(agent->energy);
    col[av->end] = (agent->state == AGENT_STATE_PRUNE)?
      0.0f : // pruning
      AGENT_RGB_ALPHA;
    av->end++;
    av->a_count++;
  }
}
