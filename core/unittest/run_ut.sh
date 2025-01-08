#!/bin/bash
# Copyright 2022 iLogtail Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# Input: /path/to/build_dir, such as build-all
buildDir=build-all
if [ "$1" = "" ]; then
	echo "Without setting build dir, use default: '$buildDir'"
else
	buildDir=$1
fi

CURRENT_DIR="$( cd "$(dirname "$0")" ; pwd -P )"
absoluteBuildDir=$CURRENT_DIR/../$buildDir
if [ ! -d "$absoluteBuildDir" ]; then
	echo "Build dir '$absoluteBuildDir' is not existing."
	exit 1
fi
echo "Build dir: $absoluteBuildDir"

CURRENT_TIME=`date +%Y-%m-%d-%H-%M-%S`
OUTPUT_DIR=$CURRENT_DIR/ut.output/$CURRENT_TIME
mkdir -p $OUTPUT_DIR
output=$OUTPUT_DIR/output.log

cd $absoluteBuildDir/unittest
if [ $? -ne 0 ]; then
	echo "Enter '$absoluteBuildDir/unittest' failed"
	exit 1
fi

# Clean up ilogtail.LOG in UTs.
for logFile in `find . -name "ilogtail.LOG"`
do
    rm $logFile
done

# Start UTs.
echo "============== common =============" >> $output
cd common
./common_simple_utils_unittest >> $output 2>&1
./common_logfileoperator_unittest >> $output 2>&1
./common_sender_queue_unittest >> $output 2>&1
./common_sliding_window_counter_unittest >> $output 2>&1
./common_string_tools_unittest >> $output 2>&1
./common_machine_info_util_unittest >> $output 2>&1
./encoding_converter_unittest >> $output 2>&1
./yaml_util_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== checkpoint ==============" >> $output
cd checkpoint
./checkpoint_managerv3_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output


echo "============== config ==============" >> $output
cd config
./config_unittest >> $output 2>&1
./config_watcher_unittest >> $output 2>&1
./config_update_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== controller ==============" >> $output
cd controller
./event_dispatcher_dir_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== event ==============" >> $output
cd event
./event_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== event_handler ==============" >> $output
cd event_handler
./log_input_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== file_source ==============" >> $output
cd file_source 
./file_discovery_options_unittest >> $output 2>&1
./multiline_options_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== input ==============" >> $output
cd input 
./input_file_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

# echo "============== models ==============" >> $output
# cd models
# ./pipeline_event_unittest >> $output 2>&1
# ./log_event_unittest >> $output 2>&1
# ./pipeline_event_ptr_unittest >> $output 2>&1
# ./pipeline_event_group_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output

echo "============== pipeline ==============" >> $output
cd pipeline
./global_config_unittest >> $output 2>&1
./pipeline_unittest >> $output 2>&1
./pipeline_manager_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output


echo "============== plugin ==============" >> $output
cd plugin
./static_input_creator_unittest >> $output 2>&1
./static_processor_creator_unittest >> $output 2>&1
./static_flusher_creator_unittest >> $output 2>&1
./input_instance_unittest >> $output 2>&1
./processor_instance_unittest >> $output 2>&1
./plugin_registry_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output


echo "============== processor ==============" >> $output
cd processor
./processor_split_log_string_native_unittest >> $output 2>&1
./processor_split_multiline_log_string_native_unittest >> $output 2>&1
./processor_parse_regex_native_unittest >> $output 2>&1
./processor_parse_json_native_unittest >> $output 2>&1
./processor_parse_timestamp_native_unittest >> $output 2>&1
./processor_tag_native_unittest >> $output 2>&1
./processor_parse_apsara_native_unittest >> $output 2>&1
./processor_parse_delimiter_native_unittest >> $output 2>&1
./processor_filter_native_unittest >> $output 2>&1
./processor_desensitize_native_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output

echo "============== reader ==============" >> $output
cd reader
./log_file_reader_deleted_file_unittest >> $output 2>&1
./file_reader_options_unittest >> $output 2>&1
./json_log_file_reader_unittest >> $output 2>&1
./remove_last_incomplete_log_unittest >> $output 2>&1
./log_file_reader_unittest >> $output 2>&1
./source_buffer_unittest >> $output 2>&1
cd ..
echo "====================================" >> $output







# echo "============== parser ==============" >> $output
# cd parser
# ./parser_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output

# echo "============== polling ==============" >> $output
# cd polling
# ./polling_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output

# echo "============== processor ==============" >> $output
# cd processor
# ./processor_filter_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output

# echo "============== reader ==============" >> $output
# cd reader
# if [ ! -d testDataSet ]; then
# 	cp -r $CURRENT_DIR/reader/testDataSet ./
# fi
# ./reader_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output

# echo "============== sender ==============" >> $output
# cd sender
# ./sender_unittest >> $output 2>&1
# cd ..

# echo "============== profiler ==============" >> $output
# cd profiler
# ./profiler_data_integrity_unittest >> $output 2>&1
# cd ..
# echo "====================================" >> $output
# echo "====================================" >> $output

# Collect logs.
for dir in `ls -al`
do
	if [ -f $dir/ilogtail.LOG ]; then
		mkdir -p $OUTPUT_DIR/$dir
		cp $dir/ilogtail.LOG $OUTPUT_DIR/$dir/
	fi
done
