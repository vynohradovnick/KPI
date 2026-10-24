IMAGE ?= stagecue-stm32-build:local

.PHONY: image build clean

image:
	docker build -t $(IMAGE) docker

build: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace \
		--user "$(shell id -u):$(shell id -g)" $(IMAGE) \
		make -C firmware all

clean:
	$(MAKE) -C firmware clean

HOST_FLAGS = -std=c11 -Wall -Wextra -Werror -Ifirmware/Core/Inc
INPUT_SOURCES = firmware/Core/Src/stage_inputs.c tests/test_stage_inputs.c
BUS_SOURCES = firmware/Core/Src/stage_bus.c firmware/Core/Src/stage_imu.c \
	firmware/Core/Src/stage_inputs.c tests/test_stage_bus.c
HOST_OUT ?= firmware/build/host
COV_CC ?= clang
LLVM_PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov
COV_FLAGS = -fprofile-instr-generate -fcoverage-mapping

.PHONY: test coverage
test:
	@mkdir -p "$(HOST_OUT)"
	$(CC) $(HOST_FLAGS) $(INPUT_SOURCES) -o "$(HOST_OUT)/test_inputs"
	$(CC) -Itests/fakes $(HOST_FLAGS) $(BUS_SOURCES) -o "$(HOST_OUT)/test_bus"
	"$(HOST_OUT)/test_inputs"
	"$(HOST_OUT)/test_bus"

coverage:
	@mkdir -p "$(HOST_OUT)"
	$(COV_CC) $(COV_FLAGS) $(HOST_FLAGS) $(INPUT_SOURCES) -o "$(HOST_OUT)/cov_inputs"
	$(COV_CC) $(COV_FLAGS) -Itests/fakes $(HOST_FLAGS) $(BUS_SOURCES) -o "$(HOST_OUT)/cov_bus"
	LLVM_PROFILE_FILE="$(HOST_OUT)/inputs.profraw" "$(HOST_OUT)/cov_inputs"
	LLVM_PROFILE_FILE="$(HOST_OUT)/bus.profraw" "$(HOST_OUT)/cov_bus"
	$(LLVM_PROFDATA) merge -sparse "$(HOST_OUT)/inputs.profraw" "$(HOST_OUT)/bus.profraw" -o "$(HOST_OUT)/stagecue.profdata"
	$(LLVM_COV) report "$(HOST_OUT)/cov_bus" -instr-profile="$(HOST_OUT)/stagecue.profdata" \
		firmware/Core/Src/stage_bus.c firmware/Core/Src/stage_imu.c firmware/Core/Src/stage_inputs.c
