#pragma once
#include <future>
#include <iostream>
#include <Eigen/Dense>
#include <limits.h>
#include "Data.hpp"
#include "tsne/tsne.h"

//Single class for data projection, differnet methods can be set in the constructor
//When created the data is projected in a separate thread
//On completion the "projected" member is set to true
//Call wait on the future object to sync
class DataProjector{
public:
    enum Method{
        PCA,
        TSNE
    };

    struct ProjectionSettings{
        //settings for PCA

        //settings for TSNE
        double perplexity, theta;
        int randSeed, maxIter, stopLyingIter, momSwitchIter;
        bool skipRandomInit;
    };

    DataProjector(const Data& data, int reducedDimensionSize, Method projectionMethod, const ProjectionSettings& settings, std::vector<uint32_t> indices = {}):data(data), reducedDimensionSize(reducedDimensionSize), projectionMethod(projectionMethod){
        if(indices.empty()){
            this->indices.resize(data.size());
            for(uint32_t i = 0; i < indices.size(); ++i) this->indices[i] = i;
        }
        else this->indices = indices;
        this->settings = settings;
        future = std::async(runAsync, this);
    }

    static void runAsync(DataProjector* p){
        p->run();
    }

    void run(){
        switch(projectionMethod){
        case Method::PCA:{
            execPCA();
        } break;
        case Method::TSNE:{
            execTSNE();
        }break;
        }
    }

    Eigen::MatrixXf projectedPoints;
    bool projected = false;
    bool interrupted = false;
    float progress = .0f;
    int reducedDimensionSize;
    std::future<void> future;

protected:
    const Data& data;
    std::vector<uint32_t> indices;
    ProjectionSettings settings;
    const float eps = 1e-6;
    Method projectionMethod;
    Eigen::MatrixXf getDataMatrix(){
        //data to eigen matrix
        Eigen::MatrixXf d(data.size(), indices.size());
        for(int i = 0; i < data.size(); ++i){
            for(int  c = 0; c < indices.size(); ++c){
                d(i,c) = data(i,indices[c]);
            }
        }
        //zero center and normalize data + perfrom svd
        Eigen::RowVectorXf meanCols = d.colwise().mean();
        d = d.rowwise() - meanCols;
        Eigen::RowVectorXf mins = d.colwise().minCoeff(), diff = d.colwise().maxCoeff() - mins;
        diff.array() += eps;
        d.array().rowwise() /= diff.array();
        progress = .33f;
        return d;
    }

    void normalizeProjectedPoints(){
        Eigen::RowVectorXf min = projectedPoints.colwise().minCoeff();
        Eigen::RowVectorXf diff = projectedPoints.colwise().maxCoeff() - min;
        projectedPoints.rowwise() -= min;
        projectedPoints.array().rowwise() /= diff.array(); 
    }

    void execPCA(){
        Eigen::MatrixXf d = getDataMatrix();
        Eigen::BDCSVD<Eigen::MatrixXf> svd(d, Eigen::ComputeThinU | Eigen::ComputeThinV);
        progress = .66f;
        //convert points to pc scores
        auto u = svd.matrixU().real();
        projectedPoints = u * svd.singularValues().real().asDiagonal();
        //drop unused scores and normalizing the points
        projectedPoints.conservativeResize(Eigen::NoChange, reducedDimensionSize);
        normalizeProjectedPoints();
        progress = 1;
        projected = true;
    }

    void execTSNE(){
        // usage of exact algorithm
        if(data.size() - 1 < 3 * settings.perplexity){
            std::cout << "Perplexity too large for the number of data points!" << std::endl;
            return;
        }
        bool exact = settings.theta == .0f;
        if(exact && data.size() * data.size() > INT_MAX){
            std::cout << "Too large dataset for exact computation, use a theta > 0 for approximation" << std::endl;
            progress = 1;
            interrupted = true;
            return;
        }

        Eigen::MatrixXd d = getDataMatrix().cast<double>();
        Eigen::MatrixXd y(d.rows(), reducedDimensionSize);

        TSNE::run(d.data(), d.rows(), d.cols(), y.data(), reducedDimensionSize, settings.perplexity, settings.theta, settings.randSeed, settings.skipRandomInit, settings.maxIter, settings.stopLyingIter, settings.momSwitchIter, &progress);
        if(progress == -1){
            interrupted = true;
            return;
        }
        projectedPoints = y.cast<float>();
        normalizeProjectedPoints();
        progress = 1;
        projected = true;
    }
};