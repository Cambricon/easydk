.. Overview

Overview
============================

Abstract
------------------------------

  EasyDevelopmentKit (hereinafter called "EDK") is a video stream processor SDK towards to MLU series. EDK can substantially simplify ability integration of inference ability with other ability, that include video codec ability, and neural network image pre-procession ability. When keep flexibility, EDK can sufficient exert MLU's hardware decode and machine learning algorithm computation's performance.

  EDK provides a set of C++ api, users could call them flexibly determins their actual needs.

  EDK provides 5 helper modules:

  Device provides MLU device context operation;

  EasyCodec provides video decode and encode function on MLU;

  EasyInfer provides neural network inference function on MLU;

  EasyBang provides Bang operator can be used without cncc and cnas, such as color-space coversion operator on MLU;

  EasyTrack provides objection track function.

EasyDevelopmentKit Software Framework
--------------------------------------

Position of EDK in Cambricon software stack as the picture below:

    .. image:: ./images/software_stack.*

    picture2.2.1 Cambricon EasyDevelopmentKit software frame picture
