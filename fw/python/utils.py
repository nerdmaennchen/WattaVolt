from contextlib import contextmanager, asynccontextmanager
import asyncio

from elevenlabs.play import is_installed

@contextmanager
def timeit(name):
    import time
    start = time.time()
    try:
        yield
    finally:
        stop = time.time()
        print(f"executing {name} took {stop-start}s")

def trace(func):
    def trace_wrap(*args, **kwargs):
        print(f"started {func}")
        try:
            return func(*args, **kwargs)
        finally:
            print(f"done {func}")
    return trace_wrap


@asynccontextmanager
async def task_guard(*tasks):
    try:
        yield
    finally:
        for t in tasks:
            try:
                t.cancel()
                await t
            except asyncio.CancelledError: # this exception will be propagated to here
                pass
            except Exception as e:
                print("when cleaning up a task an exception happened")
                print(e)
                raise


async def my_play_audio(data_gen):
    if not is_installed("mpv"):
        message = (
            "mpv not found, necessary to stream audio. "
            "On mac you can install it with 'brew install mpv'. "
            "On linux and windows you can install it from https://mpv.io/"
        )
        raise ValueError(message)

    cmd = "mpv --no-cache --no-terminal -- fd://0"
    process = await asyncio.create_subprocess_shell(
        cmd,
        stdin= asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL
    )
    async for chunk in data_gen():
        process.stdin.write(chunk)
    await process.communicate()