 # Copyright 2019 Xilinx, Inc.
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
import numpy as np
from gemx_rt import GemxRT
import operator

class KNNRT:
  def __init__(self, X, Y, in_shape, xclbin_opt):
    """
    Train the classifier. For k-nearest neighbors this is just 
    memorizing the training data.

    Input:
    X - A num_train x dimension array where each row is a training point.
    y - A vector of length num_train, where y[i] is the label for X[i, :]
    """
    self.X_train = X
    self.y_train = Y
    self.train_sum = np.sum(np.square(self.X_train), axis=1)

    bias = np.zeros((in_shape[0], self.X_train.shape[0]), dtype=np.int32, order='C')
    self.gemxRT = GemxRT (xclbin_opt, X.T, bias, wgt_scale=[1], bias_scale=[1], post_scale=[ [1, 0]])

  def predict_fpga(self, X, k=1):
    dists = self.compute_dist_fpga(X)
    return self.predict_labels(dists, k=k)

  def predict_cpu(self, X, k=1):
    """
    Predict labels for test data using this classifier.
    Input:
    X - A num_test x dimension array where each row is a test point.
    k - The number of nearest neighbors that vote for predicted label

    Output:
    y - A vector of length num_test, where y[i] is the predicted label for the
        test point X[i, :].
    """
    dists = self.compute_dist(X)
    return self.predict_labels(dists, k=k)

  def compute_dist_fpga(self, X):
    fpga_out = self.gemxRT.predict(X, 1)
    test_sum = np.sum(np.square(X), axis=1)
    #print ("predict fpga", test_sum.shape, train_sum.shape, fpga_out.shape)
    dists = np.sqrt(-2 * fpga_out + test_sum.reshape(-1, 1) + self.train_sum)
    return dists  

  def compute_dist(self, X):
    """
    Compute the distance between each test point in X and each training point
    in self.X_train using no explicit loops.
    """
    # Compute the l2 distance between all test points and all training
    # points without using any explicit loops, and store the result in
    # dists.                                                          
    # Output: sqrt((x-y)^2)
    # (x-y)^2 = x^2 + y^2 - 2xy
    test_sum = np.sum(np.square(X), axis=1)  # num_test x 1
    inner_product = np.dot(X, self.X_train.T) 
    #print ("predict cpu", test_sum.shape, train_sum.shape, inner_product.shape)
    dists = np.sqrt(-2 * inner_product + test_sum.reshape(-1, 1) + self.train_sum)
    return dists


  def getResponse(self,neighbors):
        classVotes = {}
        for x in neighbors:
            if x in classVotes:
                classVotes[x] += 1
            else:
                classVotes[x] = 1
        sortedVotes = sorted(classVotes.items(), key=operator.itemgetter(1), reverse=True)
        return sortedVotes[0][0]
    
  def predict_labels(self, dists, k=1):
    """
    Given a matrix of distances between test points and training points,
    predict a label for each test point.
    """
    num_test = dists.shape[0]
    y_pred = []
    for i in range(num_test):
      # A list of length k storing the labels of the k nearest neighbors to
      # the ith test point.
      closest_y = []
      
      # Use the distance matrix to find the k nearest neighbors of the ith    
      # training point, and use self.y_train to find the labels of these      
      # neighbors. Store these labels in closest_y.                           
      y_indices = np.argsort(dists[i, :], axis=0)
      closest_y = self.y_train[y_indices[:k]]
      y_pred.append( self.getResponse(closest_y ) )      

    return y_pred
