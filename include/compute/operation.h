/**
 * @file operation.h
 * @author Daniel Nichols
 * @version 1.0
 * @date 2019-02-18
 *
 * @copyright Copyright (c) 2019
 */
#pragma once

#include <cassert>
#include <map>
#include <string>

#include "magmadnn/config.h"
#include "tensor/tensor.h"
#if defined(MAGMADNN_HAVE_CUDA)
#include "cuda.h"   
#endif

namespace magmadnn {
namespace op {

template <typename T>
class Operation {
public:

   /** The operation class serves as an abstract object, which all tensors operations descend
     *  from. It is used to build a computation tree.
     */
   Operation() : has_been_computed(false), output_tensor(nullptr) {
#if defined(MAGMADNN_HAVE_CUDA)
      // Use default stream for CUDA kernels
      // this->custream_ = nullptr;
      this->set_custream(nullptr);

      this->set_cudnn_handle(magmadnn::internal::MAGMADNN_SETTINGS->cudnn_handle);

      this->set_cublas_handle(magmadnn::internal::MAGMADNN_SETTINGS->cublas_handle);

      this->set_async(false);
#endif
   }

   Operation(std::vector<Operation<T> *> inputs, bool needs_grad = true)
      : inputs(inputs), needs_grad(needs_grad), output_tensor(nullptr) {
      
        for (auto vit = inputs.begin(); vit != inputs.end(); vit++) {
            if (needs_grad) { /* TODO : verify this is necessary */
                (*vit)->add_consumer(this);
            }
            this->_grad_cache.insert(std::make_pair((uintptr_t)(*vit), (Tensor<T> *) NULL));
        }
        
#if defined(MAGMADNN_HAVE_CUDA)
        // Use default stream for CUDA kernels
        // this->custream_ = nullptr;
        this->set_custream(nullptr);

        this->set_cudnn_handle(magmadnn::internal::MAGMADNN_SETTINGS->cudnn_handle);

        this->set_cublas_handle(magmadnn::internal::MAGMADNN_SETTINGS->cublas_handle);

        this->set_async(false);
#endif
   }

   virtual ~Operation() {
      for (unsigned int i = 0; i < inputs.size(); i++)
           delete inputs[i];

        /*  TODO : figure out why this peice of code caused SEGFAULTS
        if (this->output_tensor != NULL) {
            delete this->output_tensor;
        }
        */
    }

    /** Returns the expected output shape of this operation.
     * @return std::vector<unsigned int>
     */
    virtual std::vector<unsigned int> get_output_shape() const { return this->output_shape; }

    /**
     * @param idx
     * @return std::vector<unsigned int>
     */
    virtual unsigned int get_output_shape(unsigned int idx) const {
        assert(idx < this->output_shape.size());
        return this->output_shape[idx];
    }

    /** The total number of elements outputted by operation.
     * @return unsigned int
     */
    virtual unsigned int get_output_size() const {
        unsigned int size = 1;
        for (unsigned int i = 0; i < this->output_shape.size(); i++) size *= this->output_shape[i];
        return size;
    }

    /** The memory type used to compute this operation.
     * @return memory_t
     */
    virtual memory_t get_memory_type() const { return this->mem_type; }

    /** Returns the operation's evaluated tensor.
     * @param recompute whether to use previous value or recalculate
     * @return Tensor<T>*
     */
    virtual Tensor<T> *eval(bool recompute = true) {
        if (!recompute && this->has_been_computed && this->output_tensor != NULL) {
            return this->output_tensor;
        } else {
            this->has_been_computed = true;
            return _eval(recompute);
        }
    }

    /** Clears the operation so that it will be recomputed.
     */
    virtual void reset() {
        this->has_been_computed = false;
        this->has_grad_been_computed = false;
    }

    /** Computes the gradient with respect to the outputs and var.
     * @param consumer the operation that consumes this that needs the gradient
     * @param grad the gradient of the loss w.r.t. the consumers output
     * @return Tensor<T>*
     */
    virtual Tensor<T> *grad(Operation<T> *consumer, Operation<T> *var, Tensor<T> *grad, bool recompute = true) {
        if (!recompute) {
            Tensor<T> *ret;
            ret = this->_grad_cache[(uintptr_t) var];

            if (ret != NULL) {
                return ret;
            } else {
                return _grad(consumer, var, grad);
            }
        } else {
            return _grad(consumer, var, grad);
        }
    }

    /**
     * @param consumer
     */
    virtual void add_consumer(Operation<T> *consumer) { this->consumers.push_back(consumer); }

    /** Returns a vector of operations that need this operation as input.
     * @return std::vector<Operation<T> *> vector of consumer operations
     */
    virtual std::vector<Operation<T> *> get_consumers() { return this->consumers; }

    /** Returns a vector the input operations to this one.
     * @return std::vector<Operation<T> *> vector of input operations
     */
    virtual std::vector<Operation<T> *> get_inputs() { return this->inputs; }

    /** Gets a pointer to the output tensor this returns
     * @return Tensor<T>*
     */
    virtual Tensor<T> *get_output_tensor() { return this->output_tensor; }

    /** Gets the current grad_tensor wrt to wrt.
     * @param wrt
     * @return Tensor<T>*
     */
    virtual Tensor<T> *get_grad_tensor(Operation<T> *wrt) { return this->_grad_cache.find((uintptr_t) wrt)->second; }

    /** string form of the given operation. Expands on input.
     * @return std::string
     */
    virtual std::string to_string() = 0;

    /** the name of this operation.
     * @return std::string
     */
    virtual std::string get_name() { return this->name; }

#if defined(MAGMADNN_HAVE_CUDA)

    /* Update CUDA execution context including CUDA stream, cuBLAS
       handle and cuDNN handle
    */
    void cuda_exec_context(CudaExecContext const& cuda_ctx) {
       // Update CUDA stream
       this->set_custream(cuda_ctx.stream());
       // Update cuBLAS handle
       this->set_cublas_handle(cuda_ctx.cublas_handle());
       // Update cuDNN handle
       this->set_cudnn_handle(cuda_ctx.cudnn_handle());
    }

    cudaStream_t get_custream() const { return custream_; }

    void set_custream(cudaStream_t custream) {

       // Make sure operations and inputs have the same custream
       for (auto vit = inputs.begin(); vit != inputs.end(); vit++) {
          Operation<T> *input_op = (*vit);
          input_op->set_custream(custream);
       }
      
       if (this->output_tensor)
          this->output_tensor->set_custream(custream);

       this->custream_ = custream;
    }

    cudnnHandle_t get_cudnn_handle() const { return cudnn_handle_; }

    void set_cudnn_handle(cudnnHandle_t cudnn_handle) {

       // Make sure operations and inputs have the same custream
       for (auto vit = inputs.begin(); vit != inputs.end(); vit++) {
          Operation<T> *input_op = (*vit);
          input_op->set_cudnn_handle(cudnn_handle);
       }
      
       this->cudnn_handle_ = cudnn_handle;
    }

    cublasHandle_t get_cublas_handle() const { return cublas_handle_; }

    void set_cublas_handle(cublasHandle_t cublas_handle) {

       // Make sure operations and inputs have the same custream
       for (auto vit = inputs.begin(); vit != inputs.end(); vit++) {
          Operation<T> *input_op = (*vit);
          input_op->set_cublas_handle(cublas_handle);
       }

       if (this->output_tensor)
          this->output_tensor->set_cublas_handle(cublas_handle);

       this->cublas_handle_ = cublas_handle;
    }

    bool get_async() const { return async_; }

    void set_async(bool async) {
       
       for (auto vit = inputs.begin(); vit != inputs.end(); vit++) {
          Operation<T> *input_op = (*vit);
          input_op->set_async(async);
       }
       
       this->async_ = async;
    }
#endif

   protected:
    /** Sets this->output_tensor to the value of this operation
     * @return Tensor<T>* the evaluated tensor
     */
    virtual Tensor<T> *_eval(bool recompute = true) = 0;

    /** Computes the gradient of this operation wrt the output of consumer.
     * @param consumer
     * @param var
     * @param grad
     * @param recompute
     * @return Tensor<T>*
     */
    virtual Tensor<T> *_grad(Operation<T> *consumer, Operation<T> *var, Tensor<T> *grad) = 0;
   
    std::vector<Operation<T> *> inputs;
    std::vector<Operation<T> *> consumers;
    std::vector<unsigned int> output_shape;
    memory_t mem_type;
    std::map<uintptr_t, Tensor<T> *> _grad_cache; /* this will cache the tensors for the gradient computation */
    std::string name = "DefaultOpName";

    Tensor<T> *output_tensor; /* the return tensor */

    bool needs_grad;
    bool has_been_computed;
    bool has_grad_been_computed;

private:
#if defined(MAGMADNN_HAVE_CUDA)
    cudaStream_t custream_;
    cudnnHandle_t cudnn_handle_;
    cublasHandle_t cublas_handle_;
   /*
    * Determine whether CUDA kernels should be called asynchronously
    */
    bool async_;
#endif
};

}  // namespace op
}  // namespace magmadnn
