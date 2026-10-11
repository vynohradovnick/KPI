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

.PHONY: test
test:
	@mkdir -p firmware/build
	$(CC) -std=c11 -Wall -Wextra -Werror -Ifirmware/Core/Inc \
		firmware/Core/Src/stage_inputs.c tests/test_stage_inputs.c \
		-o firmware/build/test_stage_inputs
	./firmware/build/test_stage_inputs
