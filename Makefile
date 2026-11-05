BUILD_ID ?= $(shell git rev-parse --short HEAD)
IMAGE ?= batchtherm-build:lab3
BUILD_DIR ?= build

.PHONY: image build clean

image:
	docker build -t $(IMAGE) docker

build: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace \
		--user "$(shell id -u):$(shell id -g)" $(IMAGE) \
		make -C firmware all BUILD_ID="$(BUILD_ID)" BUILD_DIR="$(BUILD_DIR)"

clean:
	$(MAKE) -C firmware clean

.PHONY: test check
check:
	python3 tools/check_layers.py

test: image
	docker run --rm -v "$(CURDIR):/workspace" -w /workspace --user "$(shell id -u):$(shell id -g)" $(IMAGE) sh tools/test_host.sh
