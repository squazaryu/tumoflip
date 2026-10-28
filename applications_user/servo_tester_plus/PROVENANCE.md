# Servo Tester+ source boundary

UI/control inspiration and icon: MIT-licensed M.Hasbini / Enlighten Tech,
https://github.com/gbhobbytech/flipper-servo-tester at
`50b01db47d4fefcfa2fb9a41075847c43c2b1fc7` (upstream0.5).

Tumoflip1.0 rewrites the pulse model/backend: explicit arming, session limits,
owned TIM1/Radio Broker, hardware PWM with microsecond compare and complementary
output polarity, no CPU pulse thread, managed optional 5V, stop on Back/fault.
It displays microseconds, not an unmeasured shaft-angle promise. The license
notice is retained. No precompiled upstream FAP is shipped.

Native mocks test control/ownership/compare arithmetic; physical polarity,
timing, power and actuator acceptance require measurement on the user's fixture.
Do not treat these host checks as an electrical certification.
