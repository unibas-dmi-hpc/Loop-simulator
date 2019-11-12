/*********************************************************************************
 * Copyright (c) 2018                                                            *
 * Ali Omar abdelazim Mohammed <ali.mohammed@unibas.ch>                          *
 * University of Basel, Switzerland                                              *
 * All rights reserved.                                                          *
 *                                                                               *
 * This program is free software; you can redistribute it and/or modify it       *
 * under the terms of the license (GNU LGPL) which comes with this package.      *
 *********************************************************************************/

 #include <ctype.h>


 SD_task_t last_scheduled_on_master = NULL;

 double *last_request_time;
 int *chunk_start_task_id;
 int *chunk_finish_task_id;
 SD_task_t *taskArray;


void update_chunk_time(int PE_id)
{
  if ((chunk_start_task_id[PE_id] == -1) || (chunk_finish_task_id[PE_id] == -1)|| ((chunk_finish_task_id[PE_id] - chunk_start_task_id[PE_id])<=0)) {
    previous_chunk_time[PE_id] = 0.0;
    previous_chunk_sq_time[PE_id] = 0.0;
    return ;
  }
  //printf("PE_id: %d, start task: %d, finish task: %d \n",PE_id, chunk_start_task_id[PE_id], chunk_finish_task_id[PE_id]);
  previous_chunk_time[PE_id] = SD_task_get_finish_time(taskArray[chunk_finish_task_id[PE_id]]) - SD_task_get_start_time(taskArray[chunk_start_task_id[PE_id]]);
  // calculate the sum of the squares of the tasks execution tasks times
  int i;
  previous_chunk_sq_time[PE_id] = 0.0;
  for (i = chunk_start_task_id[PE_id]; i <= chunk_finish_task_id[PE_id]; i++)
  {
    // accumulate square of the task execution time
    previous_chunk_sq_time[PE_id] += (SD_task_get_finish_time(taskArray[i]) - SD_task_get_start_time(taskArray[i]))*(SD_task_get_finish_time(taskArray[i]) - SD_task_get_start_time(taskArray[i]));
  }
 /*
  if(PE_id == 607)
  {
	printf("chunk start task: %d, chunk finish task: %d \n", chunk_start_task_id[PE_id], chunk_finish_task_id[PE_id]);
        printf("execution time: %lf \n", previous_chunk_time[PE_id] );
        
  }*/
  // restting
  chunk_start_task_id[PE_id] = -1;
  chunk_finish_task_id[PE_id] = -1;
}

void update_chunk_time_w_overheads(int PE_id)
{
  if (last_request_time[PE_id] == 0)
  {
    //printf("worker did not start yet \n");
    return ;
  }
  previous_chunk_time_w_overhead[PE_id] = SD_get_clock() - last_request_time[PE_id];
}

void init_worker_bookkeep(int num_PE)
{
  int i;
  last_request_time = malloc(num_PE*sizeof(double));
  chunk_start_task_id = malloc(num_PE*sizeof(int));
  chunk_finish_task_id = malloc(num_PE*sizeof(int));

  for (i = 0; i < num_PE; i++)
  {
    last_request_time[i] = 0;
    chunk_start_task_id[i] = -1;
    chunk_finish_task_id[i] =  -1;
  }
}


/*Create tasks for PSIA */
SD_task_t* create_PSIA_tasks(int num_tasks, int num_workers, int METHOD, char *task_times_file, int start_task)
{
  SD_task_t *tasks;
  char taskName[30];
  FILE *inputFile;
  int bufferSize = 500;
  char bufferLine[bufferSize];
  double loop_index,scalar_double,packed_128_double,packed_256_double,inst_retired,scalar_single,packed_128_single,packed_256_single,task_flops = 0;
  int i;

  // initialize tasks array
  tasks = malloc((num_tasks-start_task)*sizeof(SD_task_t));

  // read task times from file and calulate the flop amount

  //open file
  inputFile = fopen( task_times_file , "r");
  if (inputFile == NULL)
  {
    printf("Error: Can not open file: %s \n",task_times_file);
    return NULL;
  }

  int counter = 0;
  //printf("loop_index\tscalar_double\t128_packed_double\t256_packed_double\tinst_retired\tscalar_single\t128_packed_single\t256_packed_single\ttask_flops\n");
  //printf("index\ttask_flops\n");
  // read in the flops values generated by the smpi
	for (i = 0; i < num_tasks - start_task; i++) {
		fgets(bufferLine, 500, inputFile);

    //printf("%d. I read: %s\n", i, bufferLine);
		sscanf(bufferLine, "%lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf, %lf",&loop_index, &scalar_double, &packed_128_double, &packed_256_double, &inst_retired, &scalar_single, &packed_128_single, &packed_256_single, &task_flops);
    if (task_flops == 0)
    {
      i--;
      continue;
    }
    if(counter < start_task)
    {
      i--;
      counter++;
      continue;
    }
    //create squential task with the flop amount
    sprintf(taskName,"Task %d", i + start_task);
    //printf("%d\t%lf\n",i , task_flops);

    tasks[i]=SD_task_create_comp_seq(taskName, NULL, task_flops);
    //printf("created task %d, with %lf flops \n", i + start_task, task_flops);
    SD_task_watch(tasks[i], SD_DONE);

	}

  fclose(inputFile);
  return tasks;
}



void SD_schedule_task_on_host(SD_task_t task, sg_host_t host)
{

  SD_task_schedulel(task, 1, host);
  if (sg_host_is_idle(host) == false) // there is a task currently running there
  {
    //create a dependency between the last task and the current one
    SD_task_dependency_add("Resource", NULL,sg_host_get_last_scheduled_task_on_core(host, 0),task);
  }
  //update the last scheduled task to be the current task
  sg_host_set_last_scheduled_task_on_core(host, task, 0);
  // add watch to stop the simulation when the task is finished
  SD_task_watch(task, SD_DONE);
}

void SD_schedule_task_on_host_onCore(SD_task_t task, sg_host_t host, int coreID)
{

  SD_task_schedulel(task, 1, host);
  if (sg_host_is_core_idle(host, coreID) == false) // there is a task currently running there
  {
    //create a dependency between the last task and the current one
    SD_task_dependency_add("Resource", NULL,sg_host_get_last_scheduled_task_on_core(host, coreID),task);
  }
  //update the last scheduled task to be the current task
  sg_host_set_last_scheduled_task_on_core(host, task, coreID);
  // add watch to stop the simulation when the task is finished
  SD_task_watch(task, SD_DONE);
}

void SD_forward_B( sg_host_t *hosts, int start, int end,int num_cores, int num_bytes)
{

         char *comm_name = "MPI_Bcast";
        if(end- start >= 1)
        {
        // send to start + 1

         SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
         SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);
         SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
         SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
         SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);
         SD_schedule_task_on_host_onCore(comm_src, hosts[start/num_cores], start%num_cores);
         SD_schedule_task_on_host_onCore(comm_dst, hosts[(start+1)/num_cores], (start+1)%num_cores);

        //printf("host %d send to host %d \n", start, start+1);
       }
        if(end -start >= 2 )
        {
         //send to end, i.e. (end -start )/2 +1
        SD_task_t comm_src2 = SD_task_create_comp_seq("comm_src", NULL , 0);
        SD_task_t comm_dst2 = SD_task_create_comp_seq("comm_dst", NULL , 0);
        SD_task_t dummy_comm2 = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
        SD_task_dependency_add("Resource", NULL,comm_src2,dummy_comm2);
        SD_task_dependency_add("Resource", NULL,dummy_comm2,comm_dst2);
        SD_schedule_task_on_host_onCore(comm_src2, hosts[start/num_cores], start%num_cores);
        SD_schedule_task_on_host_onCore(comm_dst2, hosts[((end -start )/2 +start+1)/num_cores], ((end -start )/2 +start+1)%num_cores);

       //printf("host %d send to host %d \n", start, (end -start )/2 +start+1);
        }
         if( end- start >= 3)
        {
                SD_forward_B(hosts, start+1, (end-start)/2+ start ,num_cores,num_bytes);
                SD_forward_B(hosts, (end-start)/2 +start+1,end,num_cores, num_bytes);
        }
}


void SD_Bcast( sg_host_t *hosts,int world_size, int num_cores, int num_bytes)
{

	int start = 0;
	int end = world_size*num_cores -1 ;

	SD_forward_B(hosts, start,end,num_cores,num_bytes);
/*
  int i,j;
  char *comm_name = "MPI_Bcast";

//  sg_host_set_last_scheduled_task_on_core(hosts[0], dummy_comm, 0);
for (i = 0; i < world_size; i++)
{
  for ( j = 0; j < num_cores; j++)
  {
      SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
      SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);
      SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
      SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
      SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);
      SD_schedule_task_on_host_onCore(comm_src, hosts[0], 0);
      SD_schedule_task_on_host_onCore(comm_dst, hosts[i], 0);
    //  sg_host_set_last_scheduled_task_on_core(hosts[0], dummy_comm, 0); all communications should start at the same time
  }
}
*/
}
void SD_forward_R(sg_host_t *hosts, int start,int end,int num_cores,  int num_bytes)
{
	 char *comm_name = "MPI_Reduce";
	//printf("end : %d, start: %d\n", end, start);
        if(end- start >= 1)
        {
        // send to start + 1

         SD_task_t comm_src2 = SD_task_create_comp_seq("comm_src", NULL , 0);
         SD_task_t comm_dst2 = SD_task_create_comp_seq("comm_dst", NULL , 0);
         SD_task_t dummy_comm2 = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
         SD_task_dependency_add("Resource", NULL,comm_src2,dummy_comm2);
         SD_task_dependency_add("Resource", NULL,dummy_comm2,comm_dst2);
	 SD_schedule_task_on_host_onCore(comm_src2, hosts[(end - (end -start )/2+1)/num_cores],(end - (end -start )/2+1)%num_cores );
         SD_schedule_task_on_host_onCore(comm_dst2,  hosts[(end - (end -start )/2)/num_cores], (end - (end -start )/2)%num_cores);

	 //printf("host %d send to host %d \n",end - (end -start )/2+1, end -  (end -start )/2 );

       }
        if(end -start >= 2 )
        {
         //send to end, i.e. (end -start )/2 +1
        SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
        SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);
        SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
        SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
        SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);
	SD_schedule_task_on_host_onCore(comm_src, hosts[end/num_cores], end%num_cores);
        SD_schedule_task_on_host_onCore(comm_dst, hosts[(end - (end -start )/2)/num_cores], (end - (end -start)/2)%num_cores);


        //printf("host %d send to host %d \n", end, end - (end -start )/2);
        }
         if( end- start >= 2)
        {
                SD_forward_R(hosts, end - (end -start)/2, end - 1 ,num_cores,num_bytes);
                SD_forward_R(hosts, start, end - (end-start)/2,num_cores, num_bytes);
        }

}

void SD_Reduce( sg_host_t *hosts,int world_size, int num_cores, int num_bytes)
{
  int i,j;
  // create an end-to-end communication task
  char *comm_name = "MPI_Reduce";
	//int start = 0;
	//int end = world_size*num_cores -1;

	//SD_forward_R(hosts, start,end,num_cores, num_bytes);

  //schedule tasks on hosts
for (i = 0; i < world_size; i++)
{

    SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
    SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
    SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 1);

    //create A->dummy_comm->B dependency
    SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
    SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);
    //printf("schedule on source\n");
    SD_schedule_task_on_host_onCore(comm_src, hosts[i], 0);
    //printf("schedule on dest\n");
    SD_schedule_task_on_host_onCore(comm_dst, hosts[0], 0);
    sg_host_set_last_scheduled_task_on_core(hosts[i],dummy_comm, 0);

}


}
void SD_get_accumulate(sg_host_t source_host, sg_host_t dest_host,int source_core, int dest_core, int num_bytes, int num_flops)
{
  // create an end-to-end communication task
  char *comm_name = "MPI_Get_accummulate";
  SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, num_bytes);
  SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , num_flops);
  SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);

  //create A->dummy_comm->B dependency
  SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
  SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);

  //schedule comm_src and comm_dst on A and B
  SD_task_schedulel(comm_src, 1, source_host); //execute in the background, no task should wait this task
  SD_schedule_task_on_host_onCore(comm_dst, dest_host, dest_core);

  // update last scheduled on A // tasks on source does not depend on this communication
  //sg_host_set_last_scheduled_task_on_core(source_host,dummy_comm, source_core);
// add watch to stop the simulation when the task is finished
  SD_task_watch(dummy_comm, SD_DONE);
}

//schedule a sequential computation task and create a dependency between the
//current and the previous task to ensure their sequential execution.
/*Single core hosts support only now*/

void SD_schedule_task_on_master(SD_task_t task, sg_host_t host, int coreID)
{

  SD_task_schedulel(task, 1, host);
  if ((last_scheduled_on_master != NULL) && (SD_task_get_state(last_scheduled_on_master) != SD_DONE)) // there is a task currently running there
  {
    //create a dependency between the last task and the current one
    SD_task_dependency_add("Resource", NULL,last_scheduled_on_master,task);
  }
  //update the last scheduled task to be the current task
  last_scheduled_on_master = task;
}
//create a communication between host A and host B
// create an end-to-end communication task and make the dependencies A->comm_task->B
/*Single core hosts support only now*/
void SD_create_comm_A_B(sg_host_t host_A, sg_host_t host_B, double amount_bytes, char *comm_name)
{

  // create an end-to-end communication task
  SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, amount_bytes);
  SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
  SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);

  //create A->dummy_comm->B dependency
  SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
  SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);

  //schedule comm_src and comm_dst on A and B
  SD_schedule_task_on_host(comm_src,host_A);
  SD_schedule_task_on_host(comm_dst,host_B);

  // update last scheduled on A
  sg_host_set_last_scheduled_task_on_core(host_A,dummy_comm, 0);
// add watch to stop the simulation when the task is finished
  SD_task_watch(dummy_comm, SD_DONE);
}

void SD_create_comm_A_B_onCore_A_B(sg_host_t host_A, sg_host_t host_B, double amount_bytes, char *comm_name, int coreA, int coreB)
{

  // create an end-to-end communication task
  SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, amount_bytes);
  SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
  SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);

  //create A->dummy_comm->B dependency
  SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
  SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);

  //schedule comm_src and comm_dst on A and B
  SD_schedule_task_on_host_onCore(comm_src, host_A, coreA);
  SD_schedule_task_on_host_onCore(comm_dst, host_B, coreB);

  // update last scheduled on A
  sg_host_set_last_scheduled_task_on_core(host_A,dummy_comm, coreA);
// add watch to stop the simulation when the task is finished
  SD_task_watch(dummy_comm, SD_DONE);
}

void SD_send_to_master(sg_host_t host_A, sg_host_t master_host, double amount_bytes, char *comm_name, int coreA, int master_core)
{

  // create an end-to-end communication task
  SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, amount_bytes);
  SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
  SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);

  //create A->dummy_comm->B dependency
  SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
  SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);

  //schedule comm_src and comm_dst on A and B
  SD_schedule_task_on_host_onCore(comm_src, host_A, coreA);
  SD_schedule_task_on_master(comm_dst, master_host, master_core);

  // update last scheduled on A
  sg_host_set_last_scheduled_task_on_core(host_A,dummy_comm, coreA);
}

void SD_recieve_from_master(sg_host_t master_host, sg_host_t host_B, double amount_bytes, char *comm_name, int master_core, int coreB)
{

  // create an end-to-end communication task
  SD_task_t dummy_comm = SD_task_create_comm_e2e(comm_name, NULL, amount_bytes);
  SD_task_t comm_src = SD_task_create_comp_seq("comm_src", NULL , 0);
  SD_task_t comm_dst = SD_task_create_comp_seq("comm_dst", NULL , 0);

  //create A->dummy_comm->B dependency
  SD_task_dependency_add("Resource", NULL,comm_src,dummy_comm);
  SD_task_dependency_add("Resource", NULL,dummy_comm,comm_dst);

  //schedule comm_src and comm_dst on A and B
  SD_schedule_task_on_master(comm_src, master_host, master_core);
  SD_schedule_task_on_host_onCore(comm_dst, host_B, coreB);
}

//returns the integer number in a string
int find_number(const char *str)
{
  while (*str++!='\0') {
    if (isdigit(*str)) {
      return atoi(str);
    }
  }
  return -1;
}

bool is_contianed(int *array, int value, int length)
{
  int i;

  for (i = 0; i < length; i++) {
    if (array[i] == value)
    {
      return true;
    }
  }
  return false;
}

void append_int(int *array, int value, int length)
{
  int i;
  for (i = 0; i < length; i++)
  {
    if (array[i] == 0)
    {
      array[i] = value;
      break;
    }
  }
}



SD_task_t create_scheduling_overhead_task(int METHOD, int scheduling_step, int avail_cores, int PE_id)
{

char taskName[100];
SD_task_t calculate_chunk;

 sprintf(taskName,"calcuate chunk");

            if (METHOD == 0) {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // STATIC overhead
            }
            else if (METHOD == 1)
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 0); // SS overhead
            }
            else if (METHOD == 2)
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // FSC overhead
            }
            else if (METHOD == 3)
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // GSS overhead
            }
            else if ((METHOD == 5))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 2); // FAC overhead
            }
            else if ((METHOD == 6)&&(scheduling_step%avail_cores == 0))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 3); // WF overhead
            }
            else if ((METHOD == 6)&&(scheduling_step%avail_cores != 0))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // WF overhead
            }
            else if (((METHOD == 7) || (METHOD == 9)) &&(scheduling_step%avail_cores == 0))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, avail_cores*4 +3+3+1); // AWF-B,D overhead
                  //SD_create_comm_A_B_onCore_A_B(hosts[i], hosts[0], 16+8, "send perf stats", idle_core_ID, 0);
            }
           else if (((METHOD == 7) || (METHOD == 9)) &&(scheduling_step%avail_cores != 0))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 3+3+1); // AWF overhead
            }
           else if (((METHOD == 8) || (METHOD == 10)))
            {
                  calculate_chunk = SD_task_create_comp_seq(taskName, NULL, avail_cores*4 +3+3+1); // AWF-C,E overhead
                  //SD_create_comm_A_B_onCore_A_B(hosts[i], hosts[0], 16+8, "send perf stats", idle_core_ID, 0);
            }

            else if (METHOD == 11)
            {
              calculate_chunk = SD_task_create_comp_seq(taskName, NULL, avail_cores*4 +4*previous_chunk_size[PE_id] + 16); // AF
              //SD_create_comm_A_B_onCore_A_B(hosts[i], hosts[0], 2*16+8, "send perf stats", idle_core_ID, 0);
            }
            else if (METHOD == 12)
            {
                calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // mFSC
            }
            else if (METHOD == 4)
            {
                calculate_chunk = SD_task_create_comp_seq(taskName, NULL, 1); // TSS
            }


return calculate_chunk;
}


void send_work_request(int METHOD, int scheduling_step, int avail_cores, int PE_id, int coresperhost, sg_host_t *hosts)
{

            if (METHOD == 0) {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // STATIC overhead
            }
            else if (METHOD == 1)
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // SS overhead
            }
            else if (METHOD == 2)
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // FSC overhead
            }
            else if (METHOD == 3)
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // GSS overhead
            }
            else if ((METHOD == 5))
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // FAC overhead
            }
            else if ((METHOD == 6))
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // WF overhead
            }
            else if (((METHOD == 7) || (METHOD == 9)) &&(scheduling_step%avail_cores == 0))
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 16+8, "send work request", PE_id%coresperhost, 0);
            }
           else if (((METHOD == 7) || (METHOD == 9)) &&(scheduling_step%avail_cores != 0))
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // AWF overhead
            }
           else if (((METHOD == 8) || (METHOD == 10)))
            {
                  SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 16+8, "send work request", PE_id%coresperhost, 0); // AWF-C,E overhead

            }

            else if (METHOD == 11)
            {
              SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 2*16+8, "send work request", PE_id%coresperhost, 0); // AF
              //SD_create_comm_A_B_onCore_A_B(hosts[i], hosts[0], 2*16+8, "send perf stats", idle_core_ID, 0);
            }
            else if (METHOD == 12)
            {
               SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // mFSC

            }
            else if (METHOD == 4)
            {
               SD_create_comm_A_B_onCore_A_B(hosts[PE_id/coresperhost], hosts[0], 1, "send work request", PE_id%coresperhost, 0); // TSS
            
            }


return ;
}
