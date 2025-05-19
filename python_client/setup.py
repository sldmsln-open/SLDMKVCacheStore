from setuptools import setup, find_packages

setup(
    name="sldmkvcachestore-client",
    version="0.1.0",
    packages=find_packages(),
    install_requires=[
        "grpcio",
        "grpcio-tools",
        "protobuf",
    ],
    author="Wayne Gao",
    author_email="wayne.gao1@solidigm.com",
    description="Python client for SLDMKVCacheStore - a distributed KVCache store for vLLM",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    url="https://github.com/sldmsln-open/sldmkvcachestore",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.7",
) 