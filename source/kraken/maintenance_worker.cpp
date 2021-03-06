/* Copyright © 2001-2014, Canal TP and/or its affiliates. All rights reserved.

This file is part of Navitia,
    the software to build cool stuff with public transport.

Hope you'll enjoy and contribute to this project,
    powered by Canal TP (www.canaltp.fr).
Help us simplify mobility and open public transport:
    a non ending quest to the responsive locomotion way of traveling!

LICENCE: This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.

Stay tuned using
twitter @navitia
IRC #navitia on freenode
https://groups.google.com/d/forum/navitia
www.navitia.io
*/

#include "maintenance_worker.h"

#include "fill_disruption_from_chaos.h"
#include "type/task.pb.h"
#include "type/pt_data.h"
#include <boost/algorithm/string/join.hpp>
#include <boost/optional.hpp>
#include <sys/stat.h>
#include <signal.h>

namespace nt = navitia::type;
namespace pt = boost::posix_time;
namespace bg = boost::gregorian;


namespace navitia {


void MaintenanceWorker::load(){
    const std::string database = conf.databases_path();
    auto chaos_database = conf.chaos_database();
    auto contributors = conf.rt_topics();
    LOG4CPLUS_INFO(logger, "Loading database from file: " + database);
    if(this->data_manager.load(database, chaos_database, contributors)){
        auto data = data_manager.get_data();
    }
}


void MaintenanceWorker::operator()(){
    LOG4CPLUS_INFO(logger, "Starting background thread");
    load();

    while(true){
        try{
            this->init_rabbitmq();
            this->listen_rabbitmq();
        }catch(const std::runtime_error& ex){
            LOG4CPLUS_ERROR(logger, std::string("Connection to rabbitmq failed: ") << ex.what());
            data_manager.get_data()->is_connected_to_rabbitmq = false;
            sleep(10);
        }
    }
}

void MaintenanceWorker::handle_task(AmqpClient::Envelope::ptr_t envelope){
    LOG4CPLUS_TRACE(logger, "Task received");
    pbnavitia::Task task;
    bool result = task.ParseFromString(envelope->Message()->Body());
    if(!result){
        LOG4CPLUS_WARN(logger, "Protobuf is not valid!");
        return;
    }
    switch(task.action()){
        case pbnavitia::RELOAD:
            load(); break;
        default:
            LOG4CPLUS_TRACE(logger, "Task ignored");
    }
}

void MaintenanceWorker::handle_rt(AmqpClient::Envelope::ptr_t envelope){
    LOG4CPLUS_DEBUG(logger, "realtime info received!");
    transit_realtime::FeedMessage feed_message;
    if(! feed_message.ParseFromString(envelope->Message()->Body())){
        LOG4CPLUS_WARN(logger, "protobuf not valid!");
        return;
    }
    boost::shared_ptr<nt::Data> data;
    LOG4CPLUS_TRACE(logger, "received entity: " << feed_message.DebugString());
    for(const auto& entity: feed_message.entity()){
        if (!data) {
            data = data_manager.get_data_clone();
            data->last_rt_data_loaded = pt::microsec_clock::universal_time();
        }
        if(entity.is_deleted()){
            LOG4CPLUS_DEBUG(logger, "deletion of disruption " << entity.id());
            delete_disruption(entity.id(), *data->pt_data, *data->meta);
        }else if(entity.HasExtension(chaos::disruption)){
            LOG4CPLUS_DEBUG(logger, "add/update of disruption " << entity.id());
            add_disruption(entity.GetExtension(chaos::disruption), *data->pt_data, *data->meta);
        }else{
            LOG4CPLUS_WARN(logger, "unsupported gtfs rt feed");
        }
    }
    if (data) { data->build_raptor(); data_manager.set_data(std::move(data)); }
    LOG4CPLUS_DEBUG(logger, "data updated");
}

void MaintenanceWorker::listen_rabbitmq(){
    std::string task_tag = this->channel->BasicConsume(this->queue_name_task);
    std::string rt_tag = this->channel->BasicConsume(this->queue_name_rt);

    LOG4CPLUS_INFO(logger, "start event loop");
    data_manager.get_data()->is_connected_to_rabbitmq = true;
    while(true){
        auto envelope = this->channel->BasicConsumeMessage(std::vector<std::string>({task_tag, rt_tag}));
        if(envelope->ConsumerTag() == task_tag){
            handle_task(envelope);
        }else if(envelope->ConsumerTag() == rt_tag){
            handle_rt(envelope);
        }
    }
}

void MaintenanceWorker::init_rabbitmq(){
    std::string instance_name = conf.instance_name();
    std::string exchange_name = conf.broker_exchange();
    std::string host = conf.broker_host();
    int port = conf.broker_port();
    std::string username = conf.broker_username();
    std::string password = conf.broker_password();
    std::string vhost = conf.broker_vhost();
    //connection
    LOG4CPLUS_DEBUG(logger, boost::format("connection to rabbitmq: %s@%s:%s/%s") % username % host % port % vhost);
    this->channel = AmqpClient::Channel::Create(host, port, username, password, vhost);

    this->channel->DeclareExchange(exchange_name, "topic", false, true, false);

    //creation of a tempory queue for this kraken
    this->queue_name_task = channel->DeclareQueue("", false, false, true, true);
    //binding the queue to the exchange for all task for this instance
    channel->BindQueue(queue_name_task, exchange_name, instance_name+".task.*");

    this->queue_name_rt = channel->DeclareQueue("", false, false, true, true);
    //binding the queue to the exchange for all task for this instance
    LOG4CPLUS_INFO(logger, "subscribing to [" << boost::algorithm::join(conf.rt_topics(), ", ") << "]");
    for(auto topic: conf.rt_topics()){
        channel->BindQueue(queue_name_rt, exchange_name, topic);
    }
    LOG4CPLUS_DEBUG(logger, "connected to rabbitmq");
}

MaintenanceWorker::MaintenanceWorker(DataManager<type::Data>& data_manager, kraken::Configuration conf) :
        data_manager(data_manager),
        logger(log4cplus::Logger::getInstance(LOG4CPLUS_TEXT("background"))),
        conf(conf){}

}
