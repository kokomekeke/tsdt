# Scenario Generator
The Scenario Generator is a modular system for constructing and executing signal pipelines that simulate, electromagnetic environments.
It uses a declarative JSON-based configuration to define signal sources, transformations, and outputs.
Each scenario is executed as a real-time pipeline that can operate in one of three modes:
- **Stream Mode**: Real-time signal generation and transmission to device(s).
- **Record Mode**: Capture and save simulated signals to file(s) for later use.
- **Playback Mode**: Reproduce and stream previously recorded scenarios.

# Requirements

## Functional Requirements
- The system must support real-time streaming of IQ data through multiple transmitter devices. 
- Output must be synchronized with internal timing and sampling constraints.
- Must load scenarios from JSON configuration files specifying modules and parameters.  
- Support reading recorded signal data from file sources.
- In record mode, the system must be able to save generated IQ data to defined file formats
- Must include basic signal generators (`Sine`, `Noise`, `FSK`)
- Support chaining multiple modules (`Source`, `Processor`, `Sink`) into an executable pipeline.  
- Provide a runtime controller that handles execution.

## Non-Functional Requirements
- **Performance:** Capable of real-time IQ streaming at system-defined bandwidth.  
- **Extensibility:** New modules and parsers can be registered without modifying existing modules.  
- **Reliability:** Must ensure deterministic execution and graceful failure under load or file I/O errors.  
- **Maintainability:** Code should be modular and readable, separating parsing, pipeline construction, and runtime execution.  
- **Portability:** Must compile and run on Windows and Linux.
- **Usability:** Provide meaningful CLI help and error output for invalid or incomplete input.  


## Design Goals

- **Declarative:** Users define *what* should happen, the system handles *how*.
- **Extensible:** Supports new signal types, events, or output devices.
- **Deterministic:** Scenario execution is reproducible.
- **Real-time capable:** Supports high-throughput streaming without pregenerating the output.
- **Portable:** Runs on Windows and Linux, and is easy to install.


## High level concepts

### Scenario
- Complete description of a simulation scenario.
- Contains:
  - **Outputs** - destinations for signals (radio, file, network)
  - **Signals** - generated or prerecorded baseband transmissions
  - **Events** - time-scheduled activations of signals linked to outputs
- Defines global parameters such as duration and scenario name

### Output
- Represents a logical or physical endpoint for signal delivery.  
- Specifies operational parameters such as frequency, sample rate, and device metadata.  
- Can be shared by multiple signals.

### Signal
- Represents a baseband signal that could be transmitted on an output device.
- Can be sourced from a signal generator or IQ file.
- Generates a sequence of processing modules to create the contents of a signal.  
- Can be transmitted multiple times on different outputs.

### Event
- Scheduled occurence of a signal on a linked output.
- Specifies:
  - Start and end time
  - Transmission frequency
  - Amplitude

### Scenario Generator (package diagram)
![scenariogenerator-package](scenariogenerator-package.drawio.svg)

## Low level Pipeline Architecture

### Module
- Unit of processing separated for the purpose of modularity and parallelization.
- Modules have:
  - **Input ports** - read data from upstream queues.  
  - **Output ports** - write data to downstream queues.  
  - **Processing routine** - an algorithm to process/transform the incoming data

### Queue
- FIFO buffer connecting modules through ports.  
- Supports thread-safe read/write operations.

### Pipeline
- Graph formed by modules and queues.  
- Managed by a runtime controller that handles scheduling and synchronization.


### Abstract pipeline (class diagram)
![pipeline class diagram](pipeline-class.drawio.svg)

### Pipeline instance example (non-UML)
![pipeline example](pipeline-example.drawio.svg)

### Scenario pipeline instance example (non-UML)
![scenario pipeline example](scenario-pipeline-example.drawio.svg)

## Scenario Transformation

- Scenarios are parsed from declarative definitions (JSON).  
- These scenario elements are mapped to executable pipeline components:
  - Module instances are created.  
  - Ports are connected through queues.  
  - Events are scheduled based on time parameters.  
- Based on the Scenario Genrarator mode the executable pipelines are different:

```mermaid
---
title: Example Scenario Streaming Layout
---
graph LR
    %% Define Nodes
    FS1[File Source]
    SG[Signal Generator]
    FS2[File Source]

    TD1[Timed Demultiplexer]
    TD2[Timed Demultiplexer]
    TD3[Timed Demultiplexer]

    subgraph Top Processing Bank
        RS1[Resampler] --> TT1[Timed Tuner]
        RS2[Resampler] --> TT2[Timed Tuner]
        RS3[Resampler] --> TT3[Timed Tuner]
    end

    subgraph Bottom Processing Bank
        RS4[Resampler] --> TT4[Timed Tuner]
        RS5[Resampler] --> TT5[Timed Tuner]
        RS6[Resampler] --> TT6[Timed Tuner]
    end

    S1[Radio Transmitter]
    S2[Radio Transmitter]

    %% Define Connections
    FS1 --> TD1
    SG --> TD2
    FS2 --> TD3

    TD1 --> RS1
    TD1 --> RS4

    TD2 --> RS2
    TD2 --> RS5

    TD3 --> RS3
    TD3 --> RS6
    
    TT1 --> S1
    TT2 --> S1
    TT3 --> S1

    TT4 --> S2
    TT5 --> S2
    TT6 --> S2
```
```mermaid
---
title: Example Scenario Recording Layout
---
graph LR
    %% Define Nodes
    FS1[File Source]
    SG[Signal Generator]
    FS2[File Source]

    TD1[Timed Demultiplexer]
    TD2[Timed Demultiplexer]
    TD3[Timed Demultiplexer]

    subgraph Top Processing Bank
        RS1[Resampler] --> TT1[Timed Tuner]
        RS2[Resampler] --> TT2[Timed Tuner]
        RS3[Resampler] --> TT3[Timed Tuner]
    end

    subgraph Bottom Processing Bank
        RS4[Resampler] --> TT4[Timed Tuner]
        RS5[Resampler] --> TT5[Timed Tuner]
        RS6[Resampler] --> TT6[Timed Tuner]
    end

    S1[File Writer]
    S2[File Writer]

    %% Define Connections
    FS1 --> TD1
    SG --> TD2
    FS2 --> TD3

    TD1 --> RS1
    TD1 --> RS4

    TD2 --> RS2
    TD2 --> RS5

    TD3 --> RS3
    TD3 --> RS6
    
    TT1 --> S1
    TT2 --> S1
    TT3 --> S1

    TT4 --> S2
    TT5 --> S2
    TT6 --> S2
```

```mermaid
---
title: Example Scenario Playback Layout
---
graph LR

F1[File Source 1] --> S1[Radio Transmitter 1]
F2[File Source 2] --> S2[Radio Transmitter 2]
```

## Execution Flow

1. **Scenario Loading**  
   - Parse scenario configuration.  
   - Validate signals, outputs, and events.

2. **Pipeline Construction**  
   - Instantiate modules for each signal.  
   - Allocate queues for each port connection.  
   - Link modules in a DAG respecting event timing.  

3. **Pipeline Execution**  
   - Initialize modules.  
   - Run processing loop, reading from input ports and writing to output ports.
   - Stop execution if all modules stopped processing.  


```mermaid
sequenceDiagram
    participant Controller
    participant ModuleA
    participant Queue
    participant ModuleB

    Controller->>ModuleA: Initialize
    Controller->>ModuleB: Initialize
    
    loop Processing Loop
        Controller->>ModuleA: Process
        ModuleA->>Queue: Write signal data


        Controller->>ModuleB: Process
        ModuleB->>Queue: Read signal data
        ModuleB->>ModuleB: Process signal data
    end

    Controller->>ModuleA: Stop()
    Controller->>ModuleB: Stop()
```

