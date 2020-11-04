
### img_msgs_sync_repub
---
This package is order to forward multiple synchronized image message(`sensor_msgs::Image`) using a new frequency.

**Support parameter:**
- image message topic: cam0_topic, cam1_topic, cam2_topic...
- new_pub_hz: forward frequency
- sync_threshold: second unit

**Attention:**
Per image message's cache buffer is `50`, modify it by yourself.
