class Admin::UsersController < Admin::ApplicationController
  before_action :set_user, only: %i[show edit update destroy]
  before_action :ensure_can_administer_user, only: :destroy

  def index
    @users = User.order(created_at: :desc)
  end

  def show
  end

  def new
    @user = User.new
  end

  def create
    @user = User.new(user_params)

    if @user.save
      redirect_to admin_user_path(@user), notice: "User created."
    else
      render :new, status: :unprocessable_content
    end
  end

  def edit
  end

  def update
    if @user.update(user_params)
      redirect_to admin_user_path(@user), notice: "User updated."
    else
      render :edit, status: :unprocessable_content
    end
  end

  def destroy
    @user.destroy
    redirect_to admin_users_path, notice: "User deleted.", status: :see_other
  end

  private
    def set_user
      @user = User.find(params[:id])
    end

    def ensure_can_administer_user
      head :forbidden unless Current.user.can_administer?(@user)
    end

    def user_params
      params.expect(user: %i[first_name last_name email role password password_confirmation])
    end
end
