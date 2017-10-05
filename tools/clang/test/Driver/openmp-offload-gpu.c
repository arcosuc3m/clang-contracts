///
/// Perform several driver tests for OpenMP offloading
///

// REQUIRES: clang-driver
// REQUIRES: x86-registered-target
// REQUIRES: powerpc-registered-target
// REQUIRES: nvptx-registered-target

/// ###########################################################################

/// Check -Xopenmp-target uses one of the archs provided when several archs are used.
// RUN:   %clang -### -no-canonical-prefixes -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -Xopenmp-target -march=sm_35 -Xopenmp-target -march=sm_60 %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-FOPENMP-TARGET-ARCHS %s

// CHK-FOPENMP-TARGET-ARCHS: ptxas{{.*}}" "--gpu-name" "sm_60"
// CHK-FOPENMP-TARGET-ARCHS: nvlink{{.*}}" "-arch" "sm_60"

/// ###########################################################################

/// Check -Xopenmp-target -march=sm_35 works as expected when two triples are present.
// RUN:   %clang -### -no-canonical-prefixes -fopenmp=libomp -fopenmp-targets=powerpc64le-ibm-linux-gnu,nvptx64-nvidia-cuda -Xopenmp-target=nvptx64-nvidia-cuda -march=sm_35 %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-FOPENMP-TARGET-COMPILATION %s

// CHK-FOPENMP-TARGET-COMPILATION: ptxas{{.*}}" "--gpu-name" "sm_35"
// CHK-FOPENMP-TARGET-COMPILATION: nvlink{{.*}}" "-arch" "sm_35"

/// ###########################################################################

/// Check cubin file generation and usage by nvlink
// RUN:   %clang -### -no-canonical-prefixes -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -save-temps %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-CUBIN %s

// CHK-CUBIN: clang{{.*}}" "-o" "{{.*}}.s"
// CHK-CUBIN-NEXT: ptxas{{.*}}" "--output-file" {{.*}}.cubin" {{.*}}.s"
// CHK-CUBIN-NEXT: nvlink" {{.*}}.cubin"


/// ###########################################################################

/// Check cubin file generation and usage by nvlink when toolchain has BindArchAction
// RUN:   %clang -### -no-canonical-prefixes -target x86_64-apple-darwin17.0.0 -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-CUBIN-DARWIN %s

// CHK-CUBIN-DARWIN: clang{{.*}}" "-o" "{{.*}}.s"
// CHK-CUBIN-DARWIN-NEXT: ptxas{{.*}}" "--output-file" {{.*}}.cubin" {{.*}}.s"
// CHK-CUBIN-DARWIN-NEXT: nvlink" {{.*}}.cubin"

/// ###########################################################################

/// Check cubin file generation and usage by nvlink
// RUN:   touch %t1.o
// RUN:   touch %t2.o
// RUN:   %clang -### -no-canonical-prefixes -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda %t1.o %t2.o 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-TWOCUBIN %s

// CHK-TWOCUBIN: nvlink{{.*}}openmp-offload-{{.*}}.cubin" "{{.*}}openmp-offload-{{.*}}.cubin"

/// ###########################################################################

/// Check cubin file generation and usage by nvlink when toolchain has BindArchAction
// RUN:   touch %t1.o
// RUN:   touch %t2.o
// RUN:   %clang -### -no-canonical-prefixes -target x86_64-apple-darwin17.0.0 -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda %t1.o %t2.o 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-TWOCUBIN-DARWIN %s

// CHK-TWOCUBIN-DARWIN: nvlink{{.*}}openmp-offload-{{.*}}.cubin" "{{.*}}openmp-offload-{{.*}}.cubin"

/// ###########################################################################

/// Check PTXAS is passed -c flag when offloading to an NVIDIA device using OpenMP.
// RUN:   %clang -### -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -no-canonical-prefixes %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-PTXAS-DEFAULT %s

// CHK-PTXAS-DEFAULT: ptxas{{.*}}" "-c"

/// ###########################################################################

/// PTXAS is passed -c flag by default when offloading to an NVIDIA device using OpenMP - disable it.
// RUN:   %clang -### -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -fnoopenmp-relocatable-target -save-temps -no-canonical-prefixes %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-PTXAS-NORELO %s

// CHK-PTXAS-NORELO-NOT: ptxas{{.*}}" "-c"

/// ###########################################################################

/// PTXAS is passed -c flag by default when offloading to an NVIDIA device using OpenMP
/// Check that the flag is passed when -fopenmp-relocatable-target is used.
// RUN:   %clang -### -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -fopenmp-relocatable-target -save-temps -no-canonical-prefixes %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-PTXAS-RELO %s

// CHK-PTXAS-RELO: ptxas{{.*}}" "-c"

/// ###########################################################################

/// Check that error is not thrown by toolchain when no cuda lib flag is used.
/// Check that the flag is passed when -fopenmp-relocatable-target is used.
// RUN:   %clang -### -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -Xopenmp-target -march=sm_60 \
// RUN:   -nocudalib -fopenmp-relocatable-target -save-temps -no-canonical-prefixes %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-FLAG-NOLIBDEVICE %s

// CHK-FLAG-NOLIBDEVICE-NOT: error:{{.*}}sm_60

/// ###########################################################################

/// Check that error is not thrown by toolchain when no cuda lib device is found when using -S.
/// Check that the flag is passed when -fopenmp-relocatable-target is used.
// RUN:   %clang -### -S -fopenmp=libomp -fopenmp-targets=nvptx64-nvidia-cuda -Xopenmp-target -march=sm_60 \
// RUN:   -fopenmp-relocatable-target -save-temps -no-canonical-prefixes %s 2>&1 \
// RUN:   | FileCheck -check-prefix=CHK-NOLIBDEVICE %s

// CHK-NOLIBDEVICE-NOT: error:{{.*}}sm_60
