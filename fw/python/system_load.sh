#!/bin/bash


python python/plot.py system.load --eval "(data[1]-prev_data[1])/(data[0]-prev_data[0]),(data[2]-prev_data[2])/(data[0]-prev_data[0])"
