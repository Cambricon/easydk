.. version & Update History

Release Note
==================================

Version Record
------------------------------------

.. table:: version record

	+-----------------+--------------------------------------------------------------+
	| file name       |          Cambricon EasyDevelopmentKit Developer Guide        |
	+-----------------+--------------------------------------------------------------+
	| version         |                     V2.2.0                                   |
	+-----------------+--------------------------------------------------------------+
	| author          |                   Cambricon                                  |
	+-----------------+--------------------------------------------------------------+
	| create date     |                   2019.07.23                                 |
	+-----------------+--------------------------------------------------------------+

Update History
------------------------------------

- v1.2.0

  **update date**: 2019.7.23
  
  **release notes**:
  
  * draft version

- v2.0.0

  **update data**: 2019.11.14

  **attention**: Not Upward Compatible!
  
  **release notes**:

  	1. Rename project from CNStream-Toolkit to Easy Development Kit, and refactor all modules.

	  2. Remove useless modules (tiler, postproc, osd).

	  3. Decode, encode, vformat are merged into EasyCodec.

	  4. Support device to device memory copy.

	  5. Support KCF track algorithm.

	  6. Normalize output log.

- v2.1.0

  **update date**: 2020.8.31

  **release notes**:
    1. Add Device and EasyPlugin modules.

    2. Support output corresponding detect id of track object in EastTrack.

    3. Support get MLU core version.

    4. Set unconfirmed track object's track id as -1.

    5. Use glog instead of self-implemented log system.

- v2.2.0

	**update date**: 2020.11.05
	**release notes**:
		1. Use Cpp-style MluTaskQueue.

		2. Add API to query device number.

		3. Support to decode progresive JPEG with `turbo-jpeg` and `libyuv`.

		4. Use unified exception type.

		5. Add Resize(YUV to YUV) operator in EasyBang.

		6. Fix bugs in MluResizeConvertOp.

		7. Use new API instead of unreasonable ones:

			1. MluContext::(ConfigureForThisThread, ChannelId, SetChannelId)

			2. MluResize::InvokeOp

			3. EasyDecode::Create, EasyEncode::Create

			4. EasyInfer::(Init, Loader, BatchSize)

			5. ModelLoader::InitLayout

			6. MluMemoryOp::(SetLoader, Loader, AllocCpuInput, AllocCpuOutput, AllocMluInput, AllocMluOutput, AllocMlu, FreeArrayMlu, MemcpyInputH2D, MemcpyOutputD2H, MemcpyH2D, MemcpyD2H)