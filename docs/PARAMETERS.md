# MasterMind parameters

Generated from `Source/state/Parameters.cpp`. The IDs are frozen since M1 (spec section 9): never rename or remove an ID and never change the meaning of a choice index.

| ID | Name | Range or choices | Default | Automatable |
| --- | --- | --- | --- | --- |
| `quality` | Quality | Auto, Normal, High | Auto | no |
| `in_on` | Input On | Off / On | On | yes |
| `in_trim` | Input Trim | −24.0 dB to 24.0 dB | 0.0 dB | yes |
| `in_pol_l` | Input Polarity L | Off / On | Off | yes |
| `in_pol_r` | Input Polarity R | Off / On | Off | yes |
| `hpf_on` | Subsonic On | Off / On | Off | yes |
| `hpf_freq` | Subsonic Freq | 15 Hz to 60 Hz | 25 Hz | yes |
| `hpf_slope` | Subsonic Slope | 12 dB/oct, 24 dB/oct, 48 dB/oct | 24 dB/oct | yes |
| `eq_on` | EQ On | Off / On | Off | yes |
| `eq_phase` | EQ Phase Mode | Minimum phase, Linear phase | Minimum phase | no |
| `eq_b1_on` | EQ 1 On | Off / On | Off | yes |
| `eq_b1_type` | EQ 1 Type | Low shelf, Low cut | Low shelf | yes |
| `eq_b1_slope` | EQ 1 Slope | 6 dB/oct, 12 dB/oct, 18 dB/oct, 24 dB/oct, 36 dB/oct, 48 dB/oct | 24 dB/oct | yes |
| `eq_b1_freq` | EQ 1 Freq | 20 Hz to 20.0 kHz | 30 Hz | yes |
| `eq_b1_gain` | EQ 1 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b1_q` | EQ 1 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b1_place` | EQ 1 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b1_dyn` | EQ 1 Dynamic | Off / On | Off | yes |
| `eq_b1_thr` | EQ 1 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b1_range` | EQ 1 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b1_mode` | EQ 1 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b1_att` | EQ 1 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b1_rel` | EQ 1 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b2_on` | EQ 2 On | Off / On | Off | yes |
| `eq_b2_type` | EQ 2 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b2_freq` | EQ 2 Freq | 20 Hz to 20.0 kHz | 60 Hz | yes |
| `eq_b2_gain` | EQ 2 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b2_q` | EQ 2 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b2_place` | EQ 2 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b2_dyn` | EQ 2 Dynamic | Off / On | Off | yes |
| `eq_b2_thr` | EQ 2 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b2_range` | EQ 2 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b2_mode` | EQ 2 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b2_att` | EQ 2 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b2_rel` | EQ 2 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b3_on` | EQ 3 On | Off / On | Off | yes |
| `eq_b3_type` | EQ 3 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b3_freq` | EQ 3 Freq | 20 Hz to 20.0 kHz | 120 Hz | yes |
| `eq_b3_gain` | EQ 3 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b3_q` | EQ 3 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b3_place` | EQ 3 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b3_dyn` | EQ 3 Dynamic | Off / On | Off | yes |
| `eq_b3_thr` | EQ 3 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b3_range` | EQ 3 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b3_mode` | EQ 3 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b3_att` | EQ 3 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b3_rel` | EQ 3 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b4_on` | EQ 4 On | Off / On | Off | yes |
| `eq_b4_type` | EQ 4 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b4_freq` | EQ 4 Freq | 20 Hz to 20.0 kHz | 250 Hz | yes |
| `eq_b4_gain` | EQ 4 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b4_q` | EQ 4 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b4_place` | EQ 4 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b4_dyn` | EQ 4 Dynamic | Off / On | Off | yes |
| `eq_b4_thr` | EQ 4 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b4_range` | EQ 4 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b4_mode` | EQ 4 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b4_att` | EQ 4 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b4_rel` | EQ 4 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b5_on` | EQ 5 On | Off / On | Off | yes |
| `eq_b5_type` | EQ 5 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b5_freq` | EQ 5 Freq | 20 Hz to 20.0 kHz | 500 Hz | yes |
| `eq_b5_gain` | EQ 5 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b5_q` | EQ 5 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b5_place` | EQ 5 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b5_dyn` | EQ 5 Dynamic | Off / On | Off | yes |
| `eq_b5_thr` | EQ 5 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b5_range` | EQ 5 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b5_mode` | EQ 5 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b5_att` | EQ 5 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b5_rel` | EQ 5 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b6_on` | EQ 6 On | Off / On | Off | yes |
| `eq_b6_type` | EQ 6 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b6_freq` | EQ 6 Freq | 20 Hz to 20.0 kHz | 1.00 kHz | yes |
| `eq_b6_gain` | EQ 6 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b6_q` | EQ 6 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b6_place` | EQ 6 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b6_dyn` | EQ 6 Dynamic | Off / On | Off | yes |
| `eq_b6_thr` | EQ 6 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b6_range` | EQ 6 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b6_mode` | EQ 6 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b6_att` | EQ 6 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b6_rel` | EQ 6 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b7_on` | EQ 7 On | Off / On | Off | yes |
| `eq_b7_type` | EQ 7 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b7_freq` | EQ 7 Freq | 20 Hz to 20.0 kHz | 2.00 kHz | yes |
| `eq_b7_gain` | EQ 7 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b7_q` | EQ 7 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b7_place` | EQ 7 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b7_dyn` | EQ 7 Dynamic | Off / On | Off | yes |
| `eq_b7_thr` | EQ 7 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b7_range` | EQ 7 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b7_mode` | EQ 7 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b7_att` | EQ 7 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b7_rel` | EQ 7 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b8_on` | EQ 8 On | Off / On | Off | yes |
| `eq_b8_type` | EQ 8 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b8_freq` | EQ 8 Freq | 20 Hz to 20.0 kHz | 4.80 kHz | yes |
| `eq_b8_gain` | EQ 8 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b8_q` | EQ 8 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b8_place` | EQ 8 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b8_dyn` | EQ 8 Dynamic | Off / On | Off | yes |
| `eq_b8_thr` | EQ 8 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b8_range` | EQ 8 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b8_mode` | EQ 8 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b8_att` | EQ 8 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b8_rel` | EQ 8 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b9_on` | EQ 9 On | Off / On | Off | yes |
| `eq_b9_type` | EQ 9 Type | Bell, Low shelf, High shelf, Notch | Bell | yes |
| `eq_b9_freq` | EQ 9 Freq | 20 Hz to 20.0 kHz | 8.00 kHz | yes |
| `eq_b9_gain` | EQ 9 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b9_q` | EQ 9 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b9_place` | EQ 9 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b9_dyn` | EQ 9 Dynamic | Off / On | Off | yes |
| `eq_b9_thr` | EQ 9 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b9_range` | EQ 9 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b9_mode` | EQ 9 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b9_att` | EQ 9 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b9_rel` | EQ 9 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `eq_b10_on` | EQ 10 On | Off / On | Off | yes |
| `eq_b10_type` | EQ 10 Type | High shelf, High cut | High shelf | yes |
| `eq_b10_slope` | EQ 10 Slope | 6 dB/oct, 12 dB/oct, 18 dB/oct, 24 dB/oct, 36 dB/oct, 48 dB/oct | 24 dB/oct | yes |
| `eq_b10_freq` | EQ 10 Freq | 20 Hz to 20.0 kHz | 16.0 kHz | yes |
| `eq_b10_gain` | EQ 10 Gain | −18.0 dB to 18.0 dB | 0.0 dB | yes |
| `eq_b10_q` | EQ 10 Q | 0.10 to 18.0 | 0.71 | yes |
| `eq_b10_place` | EQ 10 Placement | Stereo, Mid, Side, Left, Right | Stereo | yes |
| `eq_b10_dyn` | EQ 10 Dynamic | Off / On | Off | yes |
| `eq_b10_thr` | EQ 10 Threshold | −60.0 dB to 0.0 dB | −24.0 dB | yes |
| `eq_b10_range` | EQ 10 Range | 0.0 dB to 18.0 dB | 6.0 dB | yes |
| `eq_b10_mode` | EQ 10 Dyn Mode | Compress, Expand | Compress | yes |
| `eq_b10_att` | EQ 10 Attack | 0.1 ms to 100 ms | 10 ms | yes |
| `eq_b10_rel` | EQ 10 Release | 5.0 ms to 1.00 s | 100 ms | yes |
| `rs_on` | Resonance Suppressor On | Off / On | Off | no |
| `rs_low` | Resonance Low | 150 Hz to 12.0 kHz | 150 Hz | yes |
| `rs_high` | Resonance High | 150 Hz to 12.0 kHz | 12.0 kHz | yes |
| `rs_depth` | Resonance Depth | 0.0 dB to 20.0 dB | 6.0 dB | yes |
| `rs_sharp` | Resonance Sharpness | 0% to 100% | 50% | yes |
| `rs_speed` | Resonance Speed | 0% to 100% | 50% | yes |
| `rs_mix` | Resonance Mix | 0% to 100% | 100% | yes |
| `rs_delta` | Resonance Delta Listen | Off / On | Off | yes |
| `mb_on` | Multiband On | Off / On | Off | yes |
| `mb_xover_type` | Multiband Crossover | Linkwitz-Riley, Linear phase | Linkwitz-Riley | no |
| `mb_xo1_freq` | Crossover 1 | 30 Hz to 16.0 kHz | 80 Hz | yes |
| `mb_xo2_freq` | Crossover 2 | 30 Hz to 16.0 kHz | 250 Hz | yes |
| `mb_xo3_freq` | Crossover 3 | 30 Hz to 16.0 kHz | 1.50 kHz | yes |
| `mb_xo4_freq` | Crossover 4 | 30 Hz to 16.0 kHz | 5.50 kHz | yes |
| `mb_b1_thr` | MB 1 Threshold | −60.0 dB to 0.0 dB | −20.0 dB | yes |
| `mb_b1_ratio` | MB 1 Ratio | 1.0:1 to 10:1 | 1.5:1 | yes |
| `mb_b1_att` | MB 1 Attack | 0.5 ms to 200 ms | 20 ms | yes |
| `mb_b1_rel` | MB 1 Release | 10 ms to 1.00 s | 200 ms | yes |
| `mb_b1_rel_auto` | MB 1 Release Auto | Off / On | Off | yes |
| `mb_b1_knee` | MB 1 Knee | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `mb_b1_makeup` | MB 1 Makeup | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b1_mix` | MB 1 Mix | 0% to 100% | 100% | yes |
| `mb_b1_link` | MB 1 Stereo Link | 0% to 100% | 100% | yes |
| `mb_b1_mode` | MB 1 Mode | Stereo, Mid/Side | Stereo | yes |
| `mb_b1_solo` | MB 1 Solo | Off / On | Off | yes |
| `mb_b1_bypass` | MB 1 Bypass | Off / On | Off | yes |
| `mb_b1_trim` | MB 1 Gain Trim | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b2_thr` | MB 2 Threshold | −60.0 dB to 0.0 dB | −20.0 dB | yes |
| `mb_b2_ratio` | MB 2 Ratio | 1.0:1 to 10:1 | 1.5:1 | yes |
| `mb_b2_att` | MB 2 Attack | 0.5 ms to 200 ms | 20 ms | yes |
| `mb_b2_rel` | MB 2 Release | 10 ms to 1.00 s | 200 ms | yes |
| `mb_b2_rel_auto` | MB 2 Release Auto | Off / On | Off | yes |
| `mb_b2_knee` | MB 2 Knee | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `mb_b2_makeup` | MB 2 Makeup | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b2_mix` | MB 2 Mix | 0% to 100% | 100% | yes |
| `mb_b2_link` | MB 2 Stereo Link | 0% to 100% | 100% | yes |
| `mb_b2_mode` | MB 2 Mode | Stereo, Mid/Side | Stereo | yes |
| `mb_b2_solo` | MB 2 Solo | Off / On | Off | yes |
| `mb_b2_bypass` | MB 2 Bypass | Off / On | Off | yes |
| `mb_b2_trim` | MB 2 Gain Trim | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b3_thr` | MB 3 Threshold | −60.0 dB to 0.0 dB | −20.0 dB | yes |
| `mb_b3_ratio` | MB 3 Ratio | 1.0:1 to 10:1 | 1.5:1 | yes |
| `mb_b3_att` | MB 3 Attack | 0.5 ms to 200 ms | 20 ms | yes |
| `mb_b3_rel` | MB 3 Release | 10 ms to 1.00 s | 200 ms | yes |
| `mb_b3_rel_auto` | MB 3 Release Auto | Off / On | Off | yes |
| `mb_b3_knee` | MB 3 Knee | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `mb_b3_makeup` | MB 3 Makeup | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b3_mix` | MB 3 Mix | 0% to 100% | 100% | yes |
| `mb_b3_link` | MB 3 Stereo Link | 0% to 100% | 100% | yes |
| `mb_b3_mode` | MB 3 Mode | Stereo, Mid/Side | Stereo | yes |
| `mb_b3_solo` | MB 3 Solo | Off / On | Off | yes |
| `mb_b3_bypass` | MB 3 Bypass | Off / On | Off | yes |
| `mb_b3_trim` | MB 3 Gain Trim | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b4_thr` | MB 4 Threshold | −60.0 dB to 0.0 dB | −20.0 dB | yes |
| `mb_b4_ratio` | MB 4 Ratio | 1.0:1 to 10:1 | 1.5:1 | yes |
| `mb_b4_att` | MB 4 Attack | 0.5 ms to 200 ms | 20 ms | yes |
| `mb_b4_rel` | MB 4 Release | 10 ms to 1.00 s | 200 ms | yes |
| `mb_b4_rel_auto` | MB 4 Release Auto | Off / On | Off | yes |
| `mb_b4_knee` | MB 4 Knee | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `mb_b4_makeup` | MB 4 Makeup | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b4_mix` | MB 4 Mix | 0% to 100% | 100% | yes |
| `mb_b4_link` | MB 4 Stereo Link | 0% to 100% | 100% | yes |
| `mb_b4_mode` | MB 4 Mode | Stereo, Mid/Side | Stereo | yes |
| `mb_b4_solo` | MB 4 Solo | Off / On | Off | yes |
| `mb_b4_bypass` | MB 4 Bypass | Off / On | Off | yes |
| `mb_b4_trim` | MB 4 Gain Trim | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b5_thr` | MB 5 Threshold | −60.0 dB to 0.0 dB | −20.0 dB | yes |
| `mb_b5_ratio` | MB 5 Ratio | 1.0:1 to 10:1 | 1.5:1 | yes |
| `mb_b5_att` | MB 5 Attack | 0.5 ms to 200 ms | 20 ms | yes |
| `mb_b5_rel` | MB 5 Release | 10 ms to 1.00 s | 200 ms | yes |
| `mb_b5_rel_auto` | MB 5 Release Auto | Off / On | Off | yes |
| `mb_b5_knee` | MB 5 Knee | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `mb_b5_makeup` | MB 5 Makeup | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `mb_b5_mix` | MB 5 Mix | 0% to 100% | 100% | yes |
| `mb_b5_link` | MB 5 Stereo Link | 0% to 100% | 100% | yes |
| `mb_b5_mode` | MB 5 Mode | Stereo, Mid/Side | Stereo | yes |
| `mb_b5_solo` | MB 5 Solo | Off / On | Off | yes |
| `mb_b5_bypass` | MB 5 Bypass | Off / On | Off | yes |
| `mb_b5_trim` | MB 5 Gain Trim | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `lem_on` | Low-End On | Off / On | Off | yes |
| `lem_mono_on` | Mono Maker On | Off / On | On | yes |
| `lem_mono_freq` | Mono Freq | 40 Hz to 300 Hz | 120 Hz | yes |
| `lem_mono_slope` | Mono Slope | 12 dB/oct, 24 dB/oct, 48 dB/oct | 24 dB/oct | yes |
| `lem_mono_amount` | Mono Amount | 0% to 100% | 100% | yes |
| `lem_rot_on` | Phase Rotator On | Off / On | Off | yes |
| `lem_rot_freq` | Rotator Freq | 30 Hz to 200 Hz | 80 Hz | yes |
| `lem_rot_angle` | Rotation | 0° to 360° | 0° | yes |
| `lem_tr_on` | Translator On | Off / On | Off | yes |
| `lem_tr_amount` | Translator Amount | 0% to 100% | 0% | yes |
| `lem_tr_mix` | Translator 2nd/3rd | 0% to 100% | 50% | yes |
| `bus_on` | Bus Comp On | Off / On | Off | yes |
| `bus_model` | Bus Comp Model | VCA, Punch | VCA | yes |
| `bus_thr` | Bus Comp Threshold | −40.0 dB to 0.0 dB | −12.0 dB | yes |
| `bus_ratio` | Bus Comp Ratio | 1.5:1, 2:1, 3:1, 4:1, 6:1, 10:1 | 2:1 | yes |
| `bus_att` | Bus Comp Attack | 0.1 ms to 30 ms | 10 ms | yes |
| `bus_rel` | Bus Comp Release | 50 ms to 1.20 s | 200 ms | yes |
| `bus_rel_auto` | Bus Comp Release Auto | Off / On | On | yes |
| `bus_knee` | Bus Comp Knee | Soft, Hard | Soft | yes |
| `bus_sc_hpf` | Bus Comp SC HPF | 20 Hz to 300 Hz | 100 Hz | yes |
| `bus_mix` | Bus Comp Mix | 0% to 100% | 100% | yes |
| `bus_detect` | Bus Comp Peak/RMS | 0% to 100% | 50% | yes |
| `bus_link` | Bus Comp Stereo Link | 0% to 100% | 100% | yes |
| `bus_makeup_auto` | Bus Comp Auto Makeup | Off / On | Off | yes |
| `sat_on` | Saturation On | Off / On | Off | yes |
| `sat_xo1_freq` | Saturation Low/Mid | 60 Hz to 500 Hz | 150 Hz | yes |
| `sat_xo2_freq` | Saturation Mid/High | 1.00 kHz to 10.0 kHz | 3.00 kHz | yes |
| `sat_b1_drive` | Saturation Low Drive | 0.0 dB to 24.0 dB | 0.0 dB | yes |
| `sat_b1_type` | Saturation Low Type | Tube, Tape, Transformer, Diode | Tube | yes |
| `sat_b1_bias` | Saturation Low Asymmetry | 0% to 100% | 0% | yes |
| `sat_b1_mix` | Saturation Low Mix | 0% to 100% | 100% | yes |
| `sat_b1_out` | Saturation Low Output | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `sat_b2_drive` | Saturation Mid Drive | 0.0 dB to 24.0 dB | 0.0 dB | yes |
| `sat_b2_type` | Saturation Mid Type | Tube, Tape, Transformer, Diode | Tube | yes |
| `sat_b2_bias` | Saturation Mid Asymmetry | 0% to 100% | 0% | yes |
| `sat_b2_mix` | Saturation Mid Mix | 0% to 100% | 100% | yes |
| `sat_b2_out` | Saturation Mid Output | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `sat_b3_drive` | Saturation High Drive | 0.0 dB to 24.0 dB | 0.0 dB | yes |
| `sat_b3_type` | Saturation High Type | Tube, Tape, Transformer, Diode | Tube | yes |
| `sat_b3_bias` | Saturation High Asymmetry | 0% to 100% | 0% | yes |
| `sat_b3_mix` | Saturation High Mix | 0% to 100% | 100% | yes |
| `sat_b3_out` | Saturation High Output | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `st_on` | Stereo On | Off / On | Off | yes |
| `st_split_freq` | Stereo Mid/High Split | 1.00 kHz to 10.0 kHz | 4.00 kHz | yes |
| `st_mid_width` | Mid Width | 0% to 200% | 100% | yes |
| `st_high_width` | High Width | 0% to 200% | 100% | yes |
| `st_ms_balance` | Mid/Side Balance | −6.0 dB to 6.0 dB | 0.0 dB | yes |
| `st_guard_on` | Correlation Guard | Off / On | Off | yes |
| `st_guard_thr` | Guard Threshold | -1.00 to 1.00 | 0.00 | yes |
| `clip_on` | Inflator On | Off / On | Off | yes |
| `clip_type` | Clipper Type | Hard, Soft, Soft knee, Diode | Soft | yes |
| `clip_thr` | Clipper Threshold | −12.0 dBFS to 0.0 dBFS | −1.0 dBFS | yes |
| `clip_shape` | Clipper Shape | 0% to 100% | 50% | yes |
| `clip_mix` | Clipper Mix | 0% to 100% | 100% | yes |
| `clip_lowprot` | Clipper Low Protect | Off / On | On | yes |
| `lim_on` | Limiter On | Off / On | Off | yes |
| `lim_lookahead` | Limiter Lookahead | 0.5 ms to 10 ms | 2.0 ms | no |
| `lim_rel` | Limiter Release | 10 ms to 500 ms | 100 ms | yes |
| `lim_rel_auto` | Limiter Release Auto | Off / On | On | yes |
| `lim_mode` | Limiter Mode | Transparent, Balanced, Aggressive | Balanced | yes |
| `lim_drive` | Limiter Drive | 0.0 dB to 24.0 dB | 0.0 dB | yes |
| `lim_ceiling` | Limiter Ceiling | −3.0 dBTP to 0.0 dBTP | −1.0 dBTP | yes |
| `lim_link` | Limiter Stereo Link | 0% to 100% | 100% | yes |
| `lim_transient` | Limiter Transients | 0% to 100% | 0% | yes |
| `out_on` | Output On | Off / On | On | yes |
| `out_gain` | Output Gain | −12.0 dB to 12.0 dB | 0.0 dB | yes |
| `out_dither` | Dither | Off, TPDF, Noise-shaped | Off | yes |
| `out_bits` | Dither Bits | 16 bit, 24 bit | 24 bit | yes |
| `sat_b1_solo` | Saturation Low Solo | Off / On | Off | yes |
| `sat_b1_mute` | Saturation Low Mute | Off / On | Off | yes |
| `sat_b2_solo` | Saturation Mid Solo | Off / On | Off | yes |
| `sat_b2_mute` | Saturation Mid Mute | Off / On | Off | yes |
| `sat_b3_solo` | Saturation High Solo | Off / On | Off | yes |
| `sat_b3_mute` | Saturation High Mute | Off / On | Off | yes |
| `clip_drive` | Clipper Drive | 0.0 dB to 24.0 dB | 0.0 dB | yes |
| `out_target` | Output Target Loudness | −30.0 LUFS to −3.0 LUFS | −9.0 LUFS | no |
| `infl_input` | Inflator Input | −6.0 dB to 12.0 dB | 0.0 dB | yes |
| `infl_effect` | Inflator Effect | 0% to 100% | 30% | yes |
| `infl_curve` | Inflator Curve | -50.0 to 50.0 | 0.00 | yes |
| `infl_output` | Inflator Output | −12.0 dB to 0.0 dB | 0.0 dB | yes |
| `infl_clip` | Inflator Clip 0 dB | Off / On | On | yes |
| `infl_split` | Inflator Band Split | Off / On | Off | yes |
| `lim_bands` | Limiter Bands | Classic, 8, 16, 26 | 26 | no |
| `lim_adapt_gain` | Limiter Adaptive Gain | 0.0 dB to 12.0 dB | 6.0 dB | yes |
| `lim_adapt_speed` | Limiter Adaptive Speed | 0% to 100% | 70% | yes |
| `lim_adapt_trans` | Limiter Adaptive Transients | 0% to 100% | 50% | yes |
| `lim_clip` | Limiter Clip | 0.0 dB to 9.0 dB | 0.0 dB | yes |
| `lim_leveler` | Limiter Leveler | 0% to 100% | 0% | yes |
| `lim_soften` | Limiter Soften | 0% to 100% | 0% | yes |
| `lim_density` | Limiter Density | 0% to 100% | 0% | yes |
| `lim_out_clip` | Limiter Output Clip | 0.0 dB to 6.0 dB | 0.0 dB | yes |
