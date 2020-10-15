#include <signal.h>
#include <iostream>
#include <queue>
#include <memory>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <thread>

#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>

std::vector<std::queue<sensor_msgs::ImageConstPtr>> img_bufs;
std::mutex mutex_buf;
std::condition_variable condition_sync;
std::thread sync_thread;

double sync_threshold = 0.003; //unit: s

void sigintHandler(int sig)
{
    std::cout << "\n===========\nHei, Get Ctr-C signal: " << sig
              << ", Start run destructor...\n" << std::endl;

    ros::shutdown();
    condition_sync.notify_all();
}

void imageCallback(const sensor_msgs::ImageConstPtr& msg, size_t index)
{
    std::unique_lock<std::mutex> lk(mutex_buf);
    img_bufs.at(index).push(msg);
    if(img_bufs.at(index).size() > 100) {
        while(img_bufs.at(index).size() > 50) {
            img_bufs.at(index).pop();
        }
        ROS_WARN_STREAM("[cam" << index << "] "
                        << "Dropping half of the image buffer.");
    }

    lk.unlock();
    condition_sync.notify_one();

    ROS_DEBUG_STREAM("[cam" << index << "] "
                     << "stamp: " << msg->header.stamp.toSec());
}

bool syncBuf(size_t index)
{
    if(index >= img_bufs.size()) {
        return true;
    }

    if(img_bufs.at(index).empty()) {
        return false;
    }

    if(index > 0) {
        double front_stamp = img_bufs.at(index-1).front()->header.stamp.toSec();
        double cur_stamp   = img_bufs.at(index  ).front()->header.stamp.toSec();
        if(front_stamp - cur_stamp > sync_threshold) {
            ROS_INFO_STREAM("Throw cur, index: " << index);
            img_bufs.at(index).pop();
            return syncBuf(index);
        }
        else if(cur_stamp - front_stamp > sync_threshold) {
            ROS_INFO_STREAM("Throw front, index-1: " << index-1);
            img_bufs.at(index-1).pop();
            return syncBuf(index-1);
        }
    }

    return syncBuf(index+1);
}

int main(int argc, char** argv)
{
    signal(SIGINT, sigintHandler);

    ros::init(argc, argv, "img_msgs_sync_repub",
              ros::init_options::NoSigintHandler);
    ros::NodeHandle n("~");
    image_transport::ImageTransport it(n);

    int img_sub_num = 0;
    std::vector<std::string> imgSubTopics;
    double new_pub_hz = 0;

    for(;;img_sub_num++) {
        std::string img_topic_name;
        std::string param_name = "cam" + std::to_string(img_sub_num) + "_topic";
        bool is_retrieved = n.param<std::string>(param_name,
                                                 img_topic_name, std::string(""));

        if(!is_retrieved) break;

        ROS_ASSERT_MSG(!img_topic_name.empty(),
                       (img_topic_name + " is empty").c_str());

        imgSubTopics.push_back(img_topic_name);
    }

    ROS_ASSERT_MSG(img_sub_num > 0,
                   ("The image sub number must be more than 0, but input: "
                   + std::to_string(img_sub_num)).c_str());


    n.param<double>("new_pub_hz", new_pub_hz, new_pub_hz);
    ROS_ASSERT_MSG(new_pub_hz > 0,
                   ("The new image pub hz can not less than 0, but input: "
                   + std::to_string(new_pub_hz)).c_str());

    n.param<double>("sync_threshold", sync_threshold, sync_threshold);
    ROS_ASSERT_MSG(sync_threshold >= 0,
                   ("The sync threshold can not less than 0, but input: "
                   + std::to_string(sync_threshold)).c_str());

    img_bufs.resize(img_sub_num);

    // create subscriber
    std::vector<image_transport::Subscriber> img_subs(img_sub_num);
    // std::vector<ros::Subscriber> img_subs(img_sub_num);
    for(size_t i=0; i < imgSubTopics.size(); i++) {
        std::cout << "sub: " << imgSubTopics.at(i) << std::endl;
        img_subs.at(i) = it.subscribe(imgSubTopics.at(i), 5,
                                     boost::bind(&imageCallback,
                                               boost::placeholders::_1, i));
    }

    // create publisher
    std::cout << "=======" << std::endl;
    std::cout << "Repub list: " << std::endl;
    std::vector<image_transport::Publisher> img_pubs(img_sub_num);
    for(size_t i=0; i < imgSubTopics.size(); i++) {
        std::string img_pub_topic_name = imgSubTopics.at(i) + "_throttle";
        img_pubs.at(i) = it.advertise(img_pub_topic_name, 1);

        std::cout << imgSubTopics.at(i) << " -> " << img_pub_topic_name << std::endl;
    }
    std::cout << std::endl;

    ROS_INFO("Start repub...");
    auto sync_loop = [&]()
    {
        int pubCount = 0;
        std::cerr << "\r" << "repub count: " << pubCount;
        ros::Time lastPubTime(0);
        while(ros::ok()) {
            std::unique_lock<std::mutex> lk(mutex_buf);
            condition_sync.wait(lk, [&]{
                if(!ros::ok()) {
                    return true;
                }

                for(size_t i=0; i < img_bufs.size(); i++) {
                    if(img_bufs.at(i).size() < 1)
                        return false;
                }

                return true;
            });

            if(!ros::ok()) break;

            if(!syncBuf(0)) {
                ROS_WARN("Sync failed");
                continue;
            }

            std::vector<sensor_msgs::ImageConstPtr> imgMsgs;
            for(size_t i=0; i < img_bufs.size(); i++) {
                imgMsgs.emplace_back(img_bufs.at(i).front());
                img_bufs.at(i).pop();
            }

            lk.unlock();

            ROS_ASSERT(img_pubs.size() == imgMsgs.size());
            double diff = imgMsgs.at(0)->header.stamp.toSec() - lastPubTime.toSec();
            ROS_ASSERT(diff > 0);
            if(diff * new_pub_hz > 1.0) {
                lastPubTime = imgMsgs.at(0)->header.stamp;
                for(size_t i=0; i < imgMsgs.size(); i++) {
                    img_pubs.at(i).publish(imgMsgs.at(i));
                }

                pubCount++;
                std::cerr << "\r" << "repub count: " << pubCount;
            }
        }
    };
    sync_thread = std::thread(sync_loop);

    ros::spin();

    if(sync_thread.joinable()) {
        sync_thread.join();
    }

    return 0;
}
