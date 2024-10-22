from typing import Callable, Iterable, Sequence, Tuple
import random
from abc import ABC, abstractmethod

import tabulate


KB = 1024
MB = 1024 * KB
GB = 1024 * MB

# ------------------------------------------------------------
# ----- Abstract Memory Model


class MemoryModel(ABC):
    @abstractmethod
    def perform_access(self, address: int) -> None: ...

    @abstractmethod
    def get_total_penalty(self) -> int: ...


class RamModel(MemoryModel):
    def __init__(self, access_penalty: int):
        super().__init__()
        self.access_penalty = access_penalty
        self.total_penalty = 0

    def perform_access(self, address: int) -> None:
        self.total_penalty += self.access_penalty

    def get_total_penalty(self) -> int:
        return self.total_penalty


class CacheModel(MemoryModel):
    def __init__(
        self, n_lines: int, line_size: int, hit_penalty: int, next_memory: MemoryModel
    ):
        assert n_lines
        assert line_size
        # Constant config
        self.n_lines = n_lines
        self.line_size = line_size
        self.hit_penalty = hit_penalty
        self.next_memory = next_memory
        # State
        self.cached_lines_queue = []
        self.cached_lines_set = set()
        self.total_penalty = 0

    def _increment_penalties(self, line_id: int, address: int) -> None:
        if line_id in self.cached_lines_set:
            self.total_penalty += self.hit_penalty
        else:
            self.next_memory.perform_access(address)

    def _add_to_cache(self, line_id: int) -> None:
        if line_id in self.cached_lines_set:
            return
        # If cache is full, remove from end of queue
        if len(self.cached_lines_queue) == self.n_lines:
            line_to_remove = self.cached_lines_queue.pop(0)
            self.cached_lines_set.remove(line_to_remove)
        # Add to start of queue
        self.cached_lines_queue.append(line_id)
        self.cached_lines_set.add(line_id)

    def perform_access(self, address: int) -> None:
        line_id = address // self.line_size
        self._increment_penalties(line_id, address)
        self._add_to_cache(line_id)

    def get_total_penalty(self) -> int:
        next_memory_penalty = self.next_memory.get_total_penalty()
        my_penalty = self.total_penalty
        return next_memory_penalty + my_penalty


def perform_accesses(model: MemoryModel, addresses: Iterable[int]) -> int:
    for addr in addresses:
        model.perform_access(addr)
    return model.get_total_penalty()


# ------------------------------------------------------------
# ----- Platform for experiments


MAX_TRIALS = 100


def mean(data: Iterable[float], eps=1e-3, n_expected_successes: int = 3) -> float:
    n = 0
    sum = 0
    mean = 0
    n_successes = 0
    for x in data:
        sum += x
        n += 1
        cur_mean = sum / n
        error = abs(mean - cur_mean)
        if error < eps:
            n_successes += 1
            if n_successes < n_expected_successes:
                print(f"🧐 Converging to {cur_mean}, err = {error}")
            else:
                print("✅ Converged to", cur_mean)
                return cur_mean
        else:
            print(f"⏳ Current mean {cur_mean}, err = {error}")
            n_successes = 0
        mean = cur_mean
    assert n, "no data"
    return mean


def run_strided_experiment(
    stride: int,
    make_memory_model: Callable[[], MemoryModel],
    generate_addresses: Callable[[int], Iterable[int]],
    max_retries: int = MAX_TRIALS,
) -> float:
    def get_penalties() -> Iterable[float]:
        for _ in range(max_retries):
            addresses = list(generate_addresses(stride))
            model = make_memory_model()
            penalty = perform_accesses(model, addresses)
            yield penalty / len(addresses)
        print(f"⛔️ Exceeded {max_retries} trials")

    return mean(get_penalties(), eps=1)


def print_results(results: Sequence[Tuple[int, float]]):
    print(
        tabulate.tabulate(
            results, headers=["Stride", "Penalty per operation"], tablefmt="psql"
        )
    )


def generate_strides_sequence(min_stride: int, max_stride: int) -> Iterable[int]:
    stride = min_stride
    while stride <= max_stride:
        yield stride
        stride *= 2


def run_strided_experiments(
    min_stride: int,
    max_stride: int,
    make_memory_model: Callable[[], MemoryModel],
    generate_addresses: Callable[[int], Iterable[int]],
    max_retries: int = MAX_TRIALS,
) -> None:
    strides = list(generate_strides_sequence(min_stride, max_stride))
    results = []
    for index, stride in enumerate(strides):
        print(f"\nStage {index + 1}/{len(strides)}: stride={stride}")
        result = run_strided_experiment(
            stride, make_memory_model, generate_addresses, max_retries
        )
        results.append((stride, result))
        print_results(results)


# ------------------------------------------------------------
# ----- Experiments


def create_memory_models() -> MemoryModel:
    DRAM = RamModel(access_penalty=1_000_000)
    # 24 MB
    L3_CACHE = CacheModel(
        n_lines=3 * KB, line_size=8 * KB, hit_penalty=10000, next_memory=DRAM
    )
    # 12 MB
    L2_CACHE = CacheModel(
        n_lines=12 * KB, line_size=KB, hit_penalty=10000, next_memory=L3_CACHE
    )
    # 128 KB
    L1_CACHE = CacheModel(
        n_lines=KB, line_size=128, hit_penalty=1, next_memory=L2_CACHE
    )
    return L1_CACHE


ARRAY_SIZE = 2 * GB
MAX_ADDRESSES = 1_000_000


def generate_random_strided_addresses(stride: int) -> Sequence[int]:
    n_strided_addresses = len(range(0, ARRAY_SIZE, stride))
    n_addresses = min([MAX_ADDRESSES, n_strided_addresses])
    result_set = set()
    result = []
    while len(result_set) < n_addresses:
        n = random.randrange(0, ARRAY_SIZE, stride)
        result_set.add(n)
        result.append(n)
    return result


def generate_directed_strided_addresses(stride: int) -> Iterable[int]:
    n_strided_addresses = len(range(0, ARRAY_SIZE, stride))
    n_addresses = min([MAX_ADDRESSES // 2, n_strided_addresses])
    for _ in range(n_addresses):
        base_addr = random.randrange(0, ARRAY_SIZE, 2 * stride)
        yield base_addr
        yield base_addr + stride


run_strided_experiments(
    min_stride=16,
    max_stride=16 * KB,
    make_memory_model=create_memory_models,
    generate_addresses=generate_directed_strided_addresses,
)
