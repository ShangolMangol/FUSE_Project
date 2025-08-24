#include <iostream>
#include <vector>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <string>

// Fast SSIM implementation in C++
class FastSSIM {
private:
    static constexpr double C1 = 0.01 * 0.01 * 255 * 255;
    static constexpr double C2 = 0.03 * 0.03 * 255 * 255;
    
public:
    static double calculateSSIM(const cv::Mat& img1, const cv::Mat& img2) {
        if (img1.size() != img2.size() || img1.type() != img2.type()) {
            std::cerr << "Images must have same size and type" << std::endl;
            return 0.0;
        }
        
        // Convert to grayscale if needed
        cv::Mat gray1, gray2;
        if (img1.channels() == 3) {
            cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
            cv::cvtColor(img2, gray2, cv::COLOR_BGR2GRAY);
        } else {
            gray1 = img1.clone();
            gray2 = img2.clone();
        }
        
        // Convert to double for calculations
        cv::Mat d1, d2;
        gray1.convertTo(d1, CV_64F);
        gray2.convertTo(d2, CV_64F);
        
        // Calculate means
        cv::Scalar mean1 = cv::mean(d1);
        cv::Scalar mean2 = cv::mean(d2);
        double mu1 = mean1[0];
        double mu2 = mean2[0];
        
        // Calculate variances and covariance
        cv::Mat d1_sq, d2_sq, d1d2;
        cv::multiply(d1, d1, d1_sq);
        cv::multiply(d2, d2, d2_sq);
        cv::multiply(d1, d2, d1d2);
        
        cv::Scalar var1 = cv::mean(d1_sq) - cv::Scalar(mu1 * mu1);
        cv::Scalar var2 = cv::mean(d2_sq) - cv::Scalar(mu2 * mu2);
        cv::Scalar cov = cv::mean(d1d2) - cv::Scalar(mu1 * mu2);
        
        double sigma1_sq = var1[0];
        double sigma2_sq = var2[0];
        double sigma12 = cov[0];
        
        // Calculate SSIM
        double numerator = (2 * mu1 * mu2 + C1) * (2 * sigma12 + C2);
        double denominator = (mu1 * mu1 + mu2 * mu2 + C1) * (sigma1_sq + sigma2_sq + C2);
        
        if (denominator == 0) {
            return 1.0; // Perfect similarity
        }
        
        return numerator / denominator;
    }
    
    static double calculateSSIMWindowed(const cv::Mat& img1, const cv::Mat& img2, int window_size = 11) {
        if (img1.size() != img2.size() || img1.type() != img2.type()) {
            std::cerr << "Images must have same size and type" << std::endl;
            return 0.0;
        }
        
        // Convert to grayscale if needed
        cv::Mat gray1, gray2;
        if (img1.channels() == 3) {
            cv::cvtColor(img1, gray1, cv::COLOR_BGR2GRAY);
            cv::cvtColor(img2, gray2, cv::COLOR_BGR2GRAY);
        } else {
            gray1 = img1.clone();
            gray2 = img2.clone();
        }
        
        // Convert to double
        cv::Mat d1, d2;
        gray1.convertTo(d1, CV_64F);
        gray2.convertTo(d2, CV_64F);
        
        // Create Gaussian window
        cv::Mat window = cv::getGaussianKernel(window_size, 1.5, CV_64F);
        cv::Mat window2d = window * window.t();
        
        // Apply windowed SSIM calculation
        cv::Mat mu1, mu2, mu1_sq, mu2_sq, mu1_mu2;
        cv::Mat sigma1_sq, sigma2_sq, sigma12;
        
        cv::filter2D(d1, mu1, CV_64F, window2d, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
        cv::filter2D(d2, mu2, CV_64F, window2d, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
        
        cv::multiply(mu1, mu1, mu1_sq);
        cv::multiply(mu2, mu2, mu2_sq);
        cv::multiply(mu1, mu2, mu1_mu2);
        
        cv::Mat d1_sq, d2_sq, d1d2;
        cv::multiply(d1, d1, d1_sq);
        cv::multiply(d2, d2, d2_sq);
        cv::multiply(d1, d2, d1d2);
        
        cv::filter2D(d1_sq, sigma1_sq, CV_64F, window2d, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
        cv::filter2D(d2_sq, sigma2_sq, CV_64F, window2d, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
        cv::filter2D(d1d2, sigma12, CV_64F, window2d, cv::Point(-1, -1), 0, cv::BORDER_REPLICATE);
        
        sigma1_sq -= mu1_sq;
        sigma2_sq -= mu2_sq;
        sigma12 -= mu1_mu2;
        
        // Calculate SSIM map
        cv::Mat numerator = (2 * mu1_mu2 + C1).mul(2 * sigma12 + C2);
        cv::Mat denominator = (mu1_sq + mu2_sq + C1).mul(sigma1_sq + sigma2_sq + C2);
        
        cv::Mat ssim_map;
        cv::divide(numerator, denominator, ssim_map);
        
        // Return mean SSIM
        return cv::mean(ssim_map)[0];
    }
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <image1> <image2> <windowed>" << std::endl;
        std::cerr << "  windowed: 0 for simple SSIM, 1 for windowed SSIM" << std::endl;
        return 1;
    }
    
    std::string img1_path = argv[1];
    std::string img2_path = argv[2];
    bool windowed = std::stoi(argv[3]) != 0;
    
    // Load images
    cv::Mat img1 = cv::imread(img1_path);
    cv::Mat img2 = cv::imread(img2_path);
    
    if (img1.empty() || img2.empty()) {
        std::cerr << "Error: Could not load images" << std::endl;
        return 1;
    }
    
    // Calculate SSIM
    double ssim_value;
    if (windowed) {
        ssim_value = FastSSIM::calculateSSIMWindowed(img1, img2);
    } else {
        ssim_value = FastSSIM::calculateSSIM(img1, img2);
    }
    
    // Output result
    std::cout << ssim_value << std::endl;
    
    return 0;
}
