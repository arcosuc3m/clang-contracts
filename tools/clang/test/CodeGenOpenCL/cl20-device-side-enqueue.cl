// RUN: %clang_cc1 %s -cl-std=CL2.0 -ffake-address-space-map -O0 -emit-llvm -o - -triple "spir-unknown-unknown" | FileCheck %s --check-prefix=COMMON --check-prefix=B32
// RUN: %clang_cc1 %s -cl-std=CL2.0 -ffake-address-space-map -O0 -emit-llvm -o - -triple "spir64-unknown-unknown" | FileCheck %s --check-prefix=COMMON --check-prefix=B64

#pragma OPENCL EXTENSION cl_khr_subgroups : enable

typedef void (^bl_t)(local void *);
typedef struct {int a;} ndrange_t;

// N.B. The check here only exists to set BL_GLOBAL
// COMMON: @block_G =  addrspace(1) constant void (i8 addrspace(3)*) addrspace(4)* addrspacecast (void (i8 addrspace(3)*) addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_GLOBAL:@__block_literal_global(\.[0-9]+)?]] to void (i8 addrspace(3)*) addrspace(1)*) to void (i8 addrspace(3)*) addrspace(4)*)
const bl_t block_G = (bl_t) ^ (local void *a) {};

kernel void device_side_enqueue(global int *a, global int *b, int i) {
  // COMMON: %default_queue = alloca %opencl.queue_t*
  queue_t default_queue;
  // COMMON: %flags = alloca i32
  unsigned flags = 0;
  // COMMON: %ndrange = alloca %struct.ndrange_t
  ndrange_t ndrange;
  // COMMON: %clk_event = alloca %opencl.clk_event_t*
  clk_event_t clk_event;
  // COMMON: %event_wait_list = alloca %opencl.clk_event_t*
  clk_event_t event_wait_list;
  // COMMON: %event_wait_list2 = alloca [1 x %opencl.clk_event_t*]
  clk_event_t event_wait_list2[] = {clk_event};

  // COMMON: [[NDR:%[a-z0-9]+]] = alloca %struct.ndrange_t, align 4
  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: [[BL:%[0-9]+]] = bitcast <{ i32, i32, i8 addrspace(4)*, i32 addrspace(1)*, i32, i32 addrspace(1)* }>* %block to void ()*
  // B64: [[BL:%[0-9]+]] = bitcast <{ i32, i32, i8 addrspace(4)*, i32 addrspace(1)*, i32 addrspace(1)*, i32 }>* %block to void ()*
  // COMMON: [[BL_I8:%[0-9]+]] = addrspacecast void ()* [[BL]] to i8 addrspace(4)*
  // COMMON: call i32 @__enqueue_kernel_basic(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* byval [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* [[BL_I8]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(void) {
                   a[i] = b[i];
                 });

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // COMMON: [[WAIT_EVNT:%[0-9]+]] = addrspacecast %opencl.clk_event_t{{.*}}** %event_wait_list to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // COMMON: [[EVNT:%[0-9]+]] = addrspacecast %opencl.clk_event_t{{.*}}** %clk_event to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // COMMON: [[BL:%[0-9]+]] = bitcast <{ i32, i32, i8 addrspace(4)*, i32{{.*}}, i32{{.*}}, i32{{.*}} }>* %block3 to void ()*
  // COMMON: [[BL_I8:%[0-9]+]] = addrspacecast void ()* [[BL]] to i8 addrspace(4)*
  // COMMON: call i32 @__enqueue_kernel_basic_events(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]],  %struct.ndrange_t* {{.*}}, i32 2, %opencl.clk_event_t{{.*}}* addrspace(4)* [[WAIT_EVNT]], %opencl.clk_event_t{{.*}}* addrspace(4)* [[EVNT]], i8 addrspace(4)* [[BL_I8]])
  enqueue_kernel(default_queue, flags, ndrange, 2, &event_wait_list, &clk_event,
                 ^(void) {
                   a[i] = b[i];
                 });

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 256, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 256, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(local void *p) {
                   return;
                 },
                 256);
  char c;
  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 %{{.*}}, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 %{{.*}}, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(local void *p) {
                   return;
                 },
                 c);

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // COMMON: [[AD:%arraydecay[0-9]*]] = getelementptr inbounds [1 x %opencl.clk_event_t*], [1 x %opencl.clk_event_t*]* %event_wait_list2, i32 0, i32 0
  // COMMON: [[WAIT_EVNT:%[0-9]+]] = addrspacecast %opencl.clk_event_t{{.*}}** [[AD]] to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // COMMON: [[EVNT:%[0-9]+]]  = addrspacecast %opencl.clk_event_t{{.*}}** %clk_event to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 256, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_events_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]],  %struct.ndrange_t* {{.*}}, i32 2, %opencl.clk_event_t{{.*}} [[WAIT_EVNT]], %opencl.clk_event_t{{.*}} [[EVNT]], i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 256, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_events_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]],  %struct.ndrange_t* {{.*}}, i32 2, %opencl.clk_event_t{{.*}} [[WAIT_EVNT]], %opencl.clk_event_t{{.*}} [[EVNT]], i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange, 2, event_wait_list2, &clk_event,
                 ^(local void *p) {
                   return;
                 },
                 256);

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // COMMON: [[AD:%arraydecay[0-9]*]] = getelementptr inbounds [1 x %opencl.clk_event_t*], [1 x %opencl.clk_event_t*]* %event_wait_list2, i32 0, i32 0
  // COMMON: [[WAIT_EVNT:%[0-9]+]] = addrspacecast %opencl.clk_event_t{{.*}}** [[AD]] to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // COMMON: [[EVNT:%[0-9]+]]  = addrspacecast %opencl.clk_event_t{{.*}}** %clk_event to %opencl.clk_event_t{{.*}}* addrspace(4)*
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 %{{.*}}, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_events_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]],  %struct.ndrange_t* {{.*}}, i32 2, %opencl.clk_event_t{{.*}}* addrspace(4)* [[WAIT_EVNT]], %opencl.clk_event_t{{.*}}* addrspace(4)* [[EVNT]], i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 %{{.*}}, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_events_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]],  %struct.ndrange_t* {{.*}}, i32 2, %opencl.clk_event_t{{.*}}* addrspace(4)* [[WAIT_EVNT]], %opencl.clk_event_t{{.*}}* addrspace(4)* [[EVNT]], i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange, 2, event_wait_list2, &clk_event,
                 ^(local void *p) {
                   return;
                 },
                 c);

  long l;
  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 %{{.*}}, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 %{{.*}}, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(local void *p) {
                   return;
                 },
                 l);

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t{{.*}}*, %opencl.queue_t{{.*}}** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: %[[TMP:.*]] = alloca [3 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [3 x i32], [3 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 1, i32* %[[TMP1]], align 4
  // B32: %[[TMP2:.*]] = getelementptr [3 x i32], [3 x i32]* %[[TMP]], i32 0, i32 1
  // B32: store i32 2, i32* %[[TMP2]], align 4
  // B32: %[[TMP3:.*]] = getelementptr [3 x i32], [3 x i32]* %[[TMP]], i32 0, i32 2
  // B32: store i32 4, i32* %[[TMP3]], align 4
  // B32: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 3, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [3 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [3 x i64], [3 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 1, i64* %[[TMP1]], align 8
  // B64: %[[TMP2:.*]] = getelementptr [3 x i64], [3 x i64]* %[[TMP]], i32 0, i32 1
  // B64: store i64 2, i64* %[[TMP2]], align 8
  // B64: %[[TMP3:.*]] = getelementptr [3 x i64], [3 x i64]* %[[TMP]], i32 0, i32 2
  // B64: store i64 4, i64* %[[TMP3]], align 8
  // B64: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 3, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(local void *p1, local void *p2, local void *p3) {
                   return;
                 },
                 1, 2, 4);

  // COMMON: [[DEF_Q:%[0-9]+]] = load %opencl.queue_t*, %opencl.queue_t** %default_queue
  // COMMON: [[FLAGS:%[0-9]+]] = load i32, i32* %flags
  // B32: %[[TMP:.*]] = alloca [1 x i32]
  // B32: %[[TMP1:.*]] = getelementptr [1 x i32], [1 x i32]* %[[TMP]], i32 0, i32 0
  // B32: store i32 0, i32* %[[TMP1]], align 4
  // B32: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i32* %[[TMP1]])
  // B64: %[[TMP:.*]] = alloca [1 x i64]
  // B64: %[[TMP1:.*]] = getelementptr [1 x i64], [1 x i64]* %[[TMP]], i32 0, i32 0
  // B64: store i64 4294967296, i64* %[[TMP1]], align 8
  // B64: call i32 @__enqueue_kernel_vaargs(%opencl.queue_t{{.*}}* [[DEF_Q]], i32 [[FLAGS]], %struct.ndrange_t* [[NDR]]{{([0-9]+)?}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* @__block_literal_global{{(.[0-9]+)?}} to i8 addrspace(1)*) to i8 addrspace(4)*), i32 1, i64* %[[TMP1]])
  enqueue_kernel(default_queue, flags, ndrange,
                 ^(local void *p) {
                   return;
                 },
                 4294967296L);

  // The full type of these expressions are long (and repeated elsewhere), so we
  // capture it as part of the regex for convenience and clarity.
  // COMMON: store void () addrspace(4)* addrspacecast (void () addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_A:@__block_literal_global(\.[0-9]+)?]] to void () addrspace(1)*) to void () addrspace(4)*), void () addrspace(4)** %block_A
  void (^const block_A)(void) = ^{
    return;
  };

  // COMMON: store void (i8 addrspace(3)*) addrspace(4)* addrspacecast (void (i8 addrspace(3)*) addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_B:@__block_literal_global(\.[0-9]+)?]] to void (i8 addrspace(3)*) addrspace(1)*) to void (i8 addrspace(3)*) addrspace(4)*), void (i8 addrspace(3)*) addrspace(4)** %block_B
  void (^const block_B)(local void *) = ^(local void *a) {
    return;
  };

  // COMMON: call i32 @__get_kernel_work_group_size_impl(i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_A]] to i8 addrspace(1)*) to i8 addrspace(4)*))
  unsigned size = get_kernel_work_group_size(block_A);
  // COMMON: call i32 @__get_kernel_work_group_size_impl(i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_B]] to i8 addrspace(1)*) to i8 addrspace(4)*))
  size = get_kernel_work_group_size(block_B);
  // COMMON: call i32 @__get_kernel_preferred_work_group_multiple_impl(i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_A]] to i8 addrspace(1)*) to i8 addrspace(4)*))
  size = get_kernel_preferred_work_group_size_multiple(block_A);
  // COMMON: call i32 @__get_kernel_preferred_work_group_multiple_impl(i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* [[BL_GLOBAL]] to i8 addrspace(1)*) to i8 addrspace(4)*))
  size = get_kernel_preferred_work_group_size_multiple(block_G);

  // COMMON: call i32 @__get_kernel_max_sub_group_size_for_ndrange_impl(%struct.ndrange_t* {{.*}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* {{.*}} to i8 addrspace(1)*) to i8 addrspace(4)*))
  size = get_kernel_max_sub_group_size_for_ndrange(ndrange, ^(){});
  // COMMON: call i32 @__get_kernel_sub_group_count_for_ndrange_impl(%struct.ndrange_t* {{.*}}, i8 addrspace(4)* addrspacecast (i8 addrspace(1)* bitcast ({ i32, i32, i8 addrspace(4)* } addrspace(1)* {{.*}} to i8 addrspace(1)*) to i8 addrspace(4)*))
  size = get_kernel_sub_group_count_for_ndrange(ndrange, ^(){});
}
